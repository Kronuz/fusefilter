/*
 * Copyright (c) 2026 Germán Méndez Bravo (Kronuz)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

// Binary fuse filters (Graf and Lemire, "Binary Fuse Filters: Fast and Smaller Than Xor
// Filters", 2022): a static set of 64-bit keys answering "maybe present" with no false
// negatives, about 9 bits per key and a 1/256 false-positive rate, in three memory reads.

#pragma once

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fusefilter {

namespace detail {

inline std::uint64_t mix(std::uint64_t h) noexcept {  // murmur3's 64-bit finalizer
	h ^= h >> 33;
	h *= 0xff51afd7ed558ccdull;
	h ^= h >> 33;
	h *= 0xc4ceb9fe1a85ec53ull;
	h ^= h >> 33;
	return h;
}

inline std::uint64_t mulhi(std::uint64_t a, std::uint64_t b) noexcept { return std::uint64_t((unsigned __int128)a * b >> 64); }

// A filter's shape and seed, and the three-slot probe that filter8 and filter8_view share.
struct layout {
	std::uint64_t seed = 0;
	std::uint32_t segment_length = 0, segment_length_mask = 0, segment_count_length = 0;

	static std::uint8_t fingerprint(std::uint64_t h) noexcept { return std::uint8_t(h ^ (h >> 32)); }

	void positions(std::uint64_t h, std::uint32_t& a, std::uint32_t& b, std::uint32_t& c) const noexcept {
		a = std::uint32_t(mulhi(h, segment_count_length));
		b = a + segment_length;
		c = b + segment_length;
		b ^= std::uint32_t(h >> 18) & segment_length_mask;
		c ^= std::uint32_t(h) & segment_length_mask;
	}

	bool contains(std::uint64_t key, const std::uint8_t* fingerprints, std::size_t n) const noexcept {
		if (n == 0) { return false; }
		const std::uint64_t h = mix(key + seed);
		std::uint32_t a, b, c;
		positions(h, a, b, c);
		return std::uint8_t(fingerprint(h) ^ fingerprints[a] ^ fingerprints[b] ^ fingerprints[c]) == 0;
	}

	// Parses serialize()'s header and returns the fingerprints' bytes within `bytes`;
	// nullopt when they are malformed.
	std::optional<std::string_view> parse(std::string_view bytes) {
		if (bytes.size() < 21 || bytes[0] != 1) { return std::nullopt; }
		seed = get<std::uint64_t>(bytes.data() + 1);
		segment_length = get<std::uint32_t>(bytes.data() + 9);
		segment_count_length = get<std::uint32_t>(bytes.data() + 13);
		const std::uint32_t length = get<std::uint32_t>(bytes.data() + 17);
		if (bytes.size() != 21 + std::size_t(length)) { return std::nullopt; }
		if (length) {
			// Positions reach segment_count_length + 2 * segment_length - 1; it must fit.
			if (!std::has_single_bit(segment_length) || segment_count_length == 0 || segment_count_length % segment_length ||
				std::uint64_t(segment_count_length) + 2ull * segment_length > length) {
				return std::nullopt;
			}
		}
		segment_length_mask = segment_length ? segment_length - 1 : 0;
		return bytes.substr(21, length);
	}

private:
	template <class T>
	static T get(const char* p) {
		T v = 0;
		for (std::size_t i = 0; i < sizeof v; ++i) { v |= T(std::uint8_t(p[i])) << (8 * i); }
		return v;
	}
};

}  // namespace detail

// A filter over a set of 64-bit keys (hash your keys first). Immutable once built.
class filter8 {
public:
	filter8() = default;

	// Builds a filter for keys (duplicates are allowed and ignored). Throws
	// std::runtime_error if construction keeps failing even as the table grows.
	static filter8 build(std::span<const std::uint64_t> keys) {
		std::vector<std::uint64_t> unique(keys.begin(), keys.end());
		return build(std::move(unique));
	}

	// The same filter, bytes and all, from keys it takes over: they are sorted and
	// deduplicated in place and left in an unspecified order, so no copy of them is made.
	// The working set past `keys` is about 24 bytes a key (8 for the peel order, 1 for its
	// slot choice, about 15 for the slot tables), against about 46 for the span overload.
	static filter8 build(std::vector<std::uint64_t>&& keys) {
		std::vector<std::uint64_t>& unique = keys;
		std::sort(unique.begin(), unique.end());
		unique.erase(std::unique(unique.begin(), unique.end()), unique.end());
		filter8 f;
		// A set near a segment-count boundary can have too few slots for any seed to peel; after
		// 100 seeds the table grows a segment (the header records its shape), so a set that
		// builds at the paper's size keeps its bytes.
		for (std::size_t extra = 0; extra < 8; ++extra) {
			f.size_layout(unique.size(), extra);
			std::uint64_t seed = 0x9e3779b97f4a7c15ull;
			for (int attempt = 0; attempt < 100; ++attempt, seed = detail::mix(seed + 1)) {
				f.l_.seed = seed;
				if (f.populate(unique)) { return f; }
			}
		}
		throw std::runtime_error("fusefilter: construction failed");
	}

	// False means the key is certainly absent; true means it is probably present.
	bool contains(std::uint64_t key) const noexcept { return l_.contains(key, fingerprints_.data(), fingerprints_.size()); }

	std::size_t size_in_bytes() const noexcept { return fingerprints_.size(); }

	// Serialization: [version u8][seed u64][segment length u32][segment count length u32]
	// [array length u32][fingerprints], little-endian.
	std::string serialize() const {
		std::string out;
		out.push_back(char(1));
		put(out, l_.seed);
		put(out, l_.segment_length);
		put(out, l_.segment_count_length);
		put(out, std::uint32_t(fingerprints_.size()));
		out.append(reinterpret_cast<const char*>(fingerprints_.data()), fingerprints_.size());
		return out;
	}

	// Parses serialize()'s bytes; nullopt when they are malformed.
	static std::optional<filter8> deserialize(std::string_view bytes) {
		filter8 f;
		const auto fingerprints = f.l_.parse(bytes);
		if (!fingerprints) { return std::nullopt; }
		const auto* p = reinterpret_cast<const std::uint8_t*>(fingerprints->data());
		f.fingerprints_.assign(p, p + fingerprints->size());
		return f;
	}

private:
	template <class T>
	static void put(std::string& out, T v) {
		for (std::size_t i = 0; i < sizeof v; ++i) { out.push_back(char(v >> (8 * i))); }
	}
	// The paper's sizing for three-wise filters: segments of a power-of-two length that
	// grows with the key count, and about 1.125x slots for large sets.
	void size_layout(std::size_t n, std::size_t extra_segments = 0) {
		if (n == 0) {
			l_.segment_length = l_.segment_length_mask = l_.segment_count_length = 0;
			fingerprints_.clear();
			return;
		}
		const double size = double(std::max<std::size_t>(n, 2));
		l_.segment_length = std::uint32_t(1) << std::min(18, int(std::floor(std::log(size) / std::log(3.33) + 2.25)));
		const double factor = std::max(1.125, 0.875 + 0.25 * std::log(1e6) / std::log(size));
		const std::size_t capacity = std::size_t(std::round(size * factor));
		std::size_t segments = (capacity + l_.segment_length - 1) / l_.segment_length;
		segments = (segments > 2 ? segments - 2 : 1) + extra_segments;
		l_.segment_count_length = std::uint32_t(segments * l_.segment_length);
		l_.segment_length_mask = l_.segment_length - 1;
		fingerprints_.assign((segments + 2) * l_.segment_length, 0);
	}

	// Peels the 3-hypergraph: repeatedly removes a slot that only one key maps to, then
	// assigns fingerprints in reverse removal order. False when the graph has a core.
	bool populate(const std::vector<std::uint64_t>& keys) {
		if (keys.empty()) { return true; }
		const std::size_t slots = fingerprints_.size();
		std::vector<std::uint32_t> count(slots, 0);
		std::vector<std::uint64_t> xored(slots, 0);
		// Hashes in slot order (a key's first slot grows with its hash), by a counting sort
		// on the top 16 bits, so the counting pass walks the slots nearly in order.
		std::vector<std::uint64_t> hashes(keys.size());
		{
			std::vector<std::uint32_t> start(65537, 0);
			for (std::uint64_t key : keys) { ++start[(detail::mix(key + l_.seed) >> 48) + 1]; }
			for (std::size_t i = 1; i < start.size(); ++i) { start[i] += start[i - 1]; }
			for (std::uint64_t key : keys) {
				const std::uint64_t h = detail::mix(key + l_.seed);
				hashes[start[h >> 48]++] = h;
			}
		}
		for (std::uint64_t h : hashes) {
			std::uint32_t p[3];
			l_.positions(h, p[0], p[1], p[2]);
			for (auto s : p) {
				++count[s];
				xored[s] ^= h;
			}
		}
		// The peel order reuses `hashes`, dead after the counting pass: the removed key's hash,
		// and which of its three slots was the lone one.
		std::vector<std::uint32_t> queue;
		for (std::uint32_t s = 0; s < slots; ++s) {
			if (count[s] == 1) { queue.push_back(s); }
		}
		std::vector<std::uint8_t> which(keys.size());
		std::size_t removed = 0;
		while (!queue.empty()) {
			const std::uint32_t s = queue.back();
			queue.pop_back();
			if (count[s] != 1) { continue; }
			const std::uint64_t h = xored[s];
			std::uint32_t p[3];
			l_.positions(h, p[0], p[1], p[2]);
			hashes[removed] = h;
			which[removed++] = std::uint8_t(p[0] == s ? 0 : p[1] == s ? 1 : 2);
			for (auto t : p) {
				--count[t];
				xored[t] ^= h;
				if (count[t] == 1) { queue.push_back(t); }
			}
		}
		if (removed != keys.size()) { return false; }
		std::fill(fingerprints_.begin(), fingerprints_.end(), 0);
		for (std::size_t i = removed; i-- > 0;) {
			std::uint32_t p[3];
			l_.positions(hashes[i], p[0], p[1], p[2]);
			std::uint8_t f = detail::layout::fingerprint(hashes[i]);
			for (int j = 0; j < 3; ++j) {
				if (j != which[i]) { f ^= fingerprints_[p[j]]; }
			}
			fingerprints_[p[which[i]]] = f;
		}
		return true;
	}

	detail::layout l_;
	std::vector<std::uint8_t> fingerprints_;
};

// A filter8 read in place from serialize()'s bytes, which must outlive it: nothing is
// copied, so a mapped file's filter costs no memory of its own.
class filter8_view {
public:
	filter8_view() = default;

	// Parses serialize()'s bytes; nullopt when they are malformed.
	static std::optional<filter8_view> parse(std::string_view bytes) {
		filter8_view v;
		const auto fingerprints = v.l_.parse(bytes);
		if (!fingerprints) { return std::nullopt; }
		v.fingerprints_ = reinterpret_cast<const std::uint8_t*>(fingerprints->data());
		v.size_ = fingerprints->size();
		return v;
	}

	// False means the key is certainly absent; true means it is probably present.
	bool contains(std::uint64_t key) const noexcept { return l_.contains(key, fingerprints_, size_); }

	std::size_t size_in_bytes() const noexcept { return size_; }

private:
	detail::layout l_;
	const std::uint8_t* fingerprints_ = nullptr;
	std::size_t size_ = 0;
};

}  // namespace fusefilter
