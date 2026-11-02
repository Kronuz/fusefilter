// libFuzzer target: any key set builds, answers present for every key, and builds the same bytes
// as the sorted set without duplicates. The first byte picks how the rest becomes keys: 8-byte
// words, or bytes, 16-bit words and 8-bit words spread over the key space, which repeat a lot.
#include "fusefilter.hh"

#include <algorithm>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
	if (size == 0) { return 0; }
	const unsigned mode = data[0] & 3;
	std::vector<std::uint64_t> keys;
	const std::uint8_t* p = data + 1;
	std::size_t left = size - 1;
	if (mode == 0) {
		for (; left >= 8 && keys.size() < 4096; p += 8, left -= 8) {
			std::uint64_t k = 0;
			for (int i = 0; i < 8; ++i) { k |= std::uint64_t(p[i]) << (8 * i); }
			keys.push_back(k);
		}
	} else if (mode == 1) {
		for (; left >= 1 && keys.size() < 4096; ++p, --left) { keys.push_back(*p); }
	} else if (mode == 2) {
		for (; left >= 2 && keys.size() < 4096; p += 2, left -= 2) { keys.push_back(std::uint64_t(p[0] | p[1] << 8) * 0x9e3779b97f4a7c15ull); }
	} else {
		for (; left >= 1 && keys.size() < 4096; ++p, --left) { keys.push_back(std::uint64_t(*p) << 56); }
	}
	const auto f = fusefilter::filter8::build(keys);
	for (auto k : keys) {
		if (!f.contains(k)) { __builtin_trap(); }
	}
	std::vector<std::uint64_t> unique = keys;
	std::sort(unique.begin(), unique.end());
	unique.erase(std::unique(unique.begin(), unique.end()), unique.end());
	if (fusefilter::filter8::build(unique).serialize() != f.serialize()) { __builtin_trap(); }
	return 0;
}
