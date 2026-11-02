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
#include <memory>
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

// What a build keeps across its attempts: uninitialized arrays, grown when a table grows.
template <class T>
struct scratch_array {
	std::unique_ptr<T[]> data;
	std::size_t capacity = 0;

	T* get(std::size_t n) {
		if (n > capacity) {
			data.reset();
			data.reset(new T[n]);
			capacity = n;
		}
		return data.get();
	}
};

struct build_scratch {
	scratch_array<std::uint64_t> hashes, xored;
	scratch_array<std::uint32_t> queue;
	scratch_array<std::uint8_t> count, which;
	bool saturated = false;  // the last attempt had a slot with 256 or more keys: its counts wrapped
};

// Asks the cache for a line that is about to be written.
inline void prefetch(const void* p) noexcept { __builtin_prefetch(p, 1, 3); }

}  // namespace detail

// A filter over a set of 64-bit keys (hash your keys first). Immutable once built.
class filter8 {
public:
	filter8() = default;

	// Builds a filter for keys (duplicates are allowed and ignored). Throws
	// std::runtime_error if construction keeps failing even as the table grows. The keys are
	// read, not copied, unless they hold duplicates (see build(std::vector&&)). The working
	// set past the filter is about 24 bytes a key: 8 for the hashes, then the peel order, 1 for
	// its slot choice, 9 for the slot tables and 4.5 for the queue.
	static filter8 build(std::span<const std::uint64_t> keys) { return build_from(keys, nullptr); }

	// The same filter, bytes and all, from keys it takes over. Only a set that holds duplicates
	// makes the build touch them: they are then sorted and deduplicated in place, and no copy
	// of them is made, where the span overload copies them.
	static filter8 build(std::vector<std::uint64_t>&& keys) { return build_from(keys, &keys); }

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

	// Builds from `keys`. Duplicates are looked for only when the first attempt to peel fails: if
	// two keys that stay in the 2-core have the same hash, the keys are deduplicated (in place in
	// `owned` when the caller took them over, else in a copy) and the search for a seed starts
	// again, so a set builds the same bytes whatever its duplicates and order.
	static filter8 build_from(std::span<const std::uint64_t> keys, std::vector<std::uint64_t>* owned) {
		filter8 f;
		detail::build_scratch w;
		std::vector<std::uint64_t> copy;
		bool checked = false;
		for (bool again = true; again;) {
			again = false;
			// A set near a segment-count boundary can have too few slots for any seed to peel; after
			// 100 seeds the table grows a segment (the header records its shape), so a set that
			// builds at the paper's size keeps its bytes.
			for (std::size_t extra = 0; extra < 8 && !again; ++extra) {
				f.size_layout(keys.size(), extra);
				std::uint64_t seed = 0x9e3779b97f4a7c15ull;
				for (int attempt = 0; attempt < 100 && !again; ++attempt, seed = detail::mix(seed + 1)) {
					f.l_.seed = seed;
					if (f.populate(keys, w)) { return f; }
					if (!checked) {
						checked = true;
						if (f.has_duplicates(keys, w)) {
							std::vector<std::uint64_t>& unique = owned ? *owned : copy;
							if (!owned) { unique.assign(keys.begin(), keys.end()); }
							std::sort(unique.begin(), unique.end());
							unique.erase(std::unique(unique.begin(), unique.end()), unique.end());
							keys = unique;
							again = true;
						}
					}
				}
			}
		}
		throw std::runtime_error("fusefilter: construction failed");
	}

	// The slots that a bucket's hashes pick first are near each other (a hash's first slot
	// grows with the hash), so the counting pass walks them nearly in order: the hashes are
	// grouped by their top bits, 4096 or so a group.
	static int bucket_bits(std::size_t n) noexcept { return std::clamp(int(std::bit_width(n >> 12)), 1, 10); }

	// Peels the 3-hypergraph: repeatedly removes a slot that only one key maps to, then
	// assigns fingerprints in reverse removal order. False when the graph has a core, and
	// then w.count is what the peel left, or when a slot's count wrapped (w.saturated).
	bool populate(std::span<const std::uint64_t> keys, detail::build_scratch& w) {
		const std::size_t n = keys.size();
		w.saturated = false;
		if (n == 0) { return true; }
		const std::size_t slots = fingerprints_.size();
		std::uint64_t* const hashes = w.hashes.get(n);
		std::uint8_t* const which = w.which.get(n);
		std::uint8_t* const count = w.count.get(slots);
		std::uint64_t* const xored = w.xored.get(slots);
		std::uint32_t* const queue = w.queue.get(slots + 3);
		{
			const int shift = 64 - bucket_bits(n);
			std::uint32_t start[(1u << 10) + 1] = {};
			for (std::uint64_t key : keys) { ++start[(detail::mix(key + l_.seed) >> shift) + 1]; }
			for (std::size_t i = 1; i < (std::size_t(1) << (64 - shift)) + 1; ++i) { start[i] += start[i - 1]; }
			for (std::uint64_t key : keys) {
				const std::uint64_t h = detail::mix(key + l_.seed);
				hashes[start[h >> shift]++] = h;
			}
		}
		std::memset(count, 0, slots * sizeof *count);
		std::memset(xored, 0, slots * sizeof *xored);
		for (std::size_t i = 0; i < n; ++i) {
			const std::uint64_t h = hashes[i];
			std::uint32_t p[3];
			l_.positions(h, p[0], p[1], p[2]);
			for (auto s : p) {
				++count[s];
				xored[s] ^= h;
			}
		}
		// The queue starts with the slots that hold one key, in slot order. A slot is queued once,
		// when its count first is 1, so `queue` never holds more than `slots` entries; the stores
		// that do not queue are overwritten. The counts are bytes: they add up to 3n unless one wrapped.
		std::size_t tail = 0, total = 0;
		for (std::uint32_t s = 0; s < slots; ++s) {
			queue[tail] = s;
			tail += count[s] == 1;
			total += count[s];
		}
		if (total != 3 * n) {
			w.saturated = true;
			return false;
		}
		// The peel order reuses `hashes`, dead after the counting pass: the removed key's hash,
		// and which of its three slots was the lone one. Slots are taken in the order they were
		// queued, so the ones ahead are known: their lines are fetched `slot_ahead` entries early,
		// and the lines of the slots the key behind them will touch, `key_ahead` entries early.
		// The lone slot of a removed key is left as it is: nothing reads it again, and a removed
		// key keeps one slot with a count of 1, which has_duplicates reads as outside the core.
		constexpr std::size_t slot_ahead = 16, key_ahead = 8;
		std::size_t head = 0, removed = 0;
		while (head < tail) {
			if (head + slot_ahead < tail) {
				const std::uint32_t t = queue[head + slot_ahead];
				detail::prefetch(count + t);
				detail::prefetch(xored + t);
			}
			if (head + key_ahead < tail) {
				std::uint32_t q[3];
				l_.positions(xored[queue[head + key_ahead]], q[0], q[1], q[2]);
				for (auto t : q) {
					detail::prefetch(count + t);
					detail::prefetch(xored + t);
				}
			}
			const std::uint32_t s = queue[head++];
			if (count[s] != 1) { continue; }
			const std::uint64_t h = xored[s];
			std::uint32_t p[3];
			l_.positions(h, p[0], p[1], p[2]);
			hashes[removed] = h;
			const unsigned lone = (p[0] != s) + (p[0] != s && p[1] != s);
			which[removed++] = std::uint8_t(lone);
			for (const std::uint32_t t : {lone == 0 ? p[1] : p[0], lone == 2 ? p[1] : p[2]}) {
				--count[t];
				xored[t] ^= h;
				queue[tail] = t;
				tail += count[t] == 1;
			}
		}
		if (removed != n) { return false; }
		// Every slot is still 0 here, and a key's lone slot is only written by that key, so the
		// XOR of all three slots with the lone one still 0 is the XOR of the other two.
		std::uint8_t* const fp = fingerprints_.data();
		for (std::size_t i = removed; i-- > 0;) {
			const std::uint64_t h = hashes[i];
			std::uint32_t p[3];
			l_.positions(h, p[0], p[1], p[2]);
			fp[p[which[i]]] = std::uint8_t(detail::layout::fingerprint(h) ^ fp[p[0]] ^ fp[p[1]] ^ fp[p[2]]);
		}
		return true;
	}

	// After a populate that failed: do two keys have the same hash? Every duplicate stays in the
	// 2-core (two equal edges are never peeled), and a key is in the core exactly when each of
	// its slots has two or more keys left, so only those keys' hashes are collected and sorted.
	// When a count wrapped the counts say nothing, and every hash is sorted.
	bool has_duplicates(std::span<const std::uint64_t> keys, const detail::build_scratch& w) const {
		const std::uint8_t* const count = w.count.data.get();
		std::vector<std::uint64_t> core;
		for (std::uint64_t key : keys) {
			const std::uint64_t h = detail::mix(key + l_.seed);
			std::uint32_t p[3];
			l_.positions(h, p[0], p[1], p[2]);
			if (w.saturated || (count[p[0]] >= 2 && count[p[1]] >= 2 && count[p[2]] >= 2)) { core.push_back(h); }
		}
		std::sort(core.begin(), core.end());
		return std::adjacent_find(core.begin(), core.end()) != core.end();
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
