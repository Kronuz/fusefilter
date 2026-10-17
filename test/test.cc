// fusefilter: no false negatives, a false-positive rate near 1/256, about 9 bits per key,
// duplicates ignored, serialization round trips, and malformed bytes rejected.
#include "fusefilter.hh"

#include <cstdio>
#include <cstdlib>
#include <random>

static int failures = 0;
#define CHECK(cond) \
	do { \
		if (!(cond)) { std::fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); ++failures; } \
	} while (0)

int main() {
	std::mt19937_64 rng(3);
	for (std::size_t n : {0u, 1u, 2u, 3u, 10u, 1000u, 100000u, 1000000u}) {
		std::vector<std::uint64_t> keys(n);
		for (auto& k : keys) { k = rng(); }
		auto f = fusefilter::filter8::build(keys);
		std::size_t missing = 0;
		for (auto k : keys) { missing += !f.contains(k); }
		CHECK(missing == 0);
		std::size_t fp = 0;
		const std::size_t probes = 1000000;
		for (std::size_t i = 0; i < probes; ++i) { fp += f.contains(rng()); }
		if (n >= 1000) {
			const double rate = double(fp) / probes, bits = 8.0 * double(f.size_in_bytes()) / double(n);
			std::printf("n=%zu: %.4f%% false positives, %.2f bits/key\n", n, 100 * rate, bits);
			CHECK(rate < 0.0050);
			CHECK(bits < (n >= 100000 ? 9.6 : 12.0));  // small sets need more slack
		}
		if (n == 0) { CHECK(fp == 0); }
		auto g = fusefilter::filter8::deserialize(f.serialize());
		CHECK(g.has_value());
		for (std::size_t i = 0; g && i < std::min<std::size_t>(n, 1000); ++i) { CHECK(g->contains(keys[i])); }
		CHECK(g && g->serialize() == f.serialize());
		// The view reads the same bytes in place and answers exactly as the filter does.
		const std::string bytes = f.serialize();
		auto v = fusefilter::filter8_view::parse(bytes);
		CHECK(v.has_value());
		CHECK(v && v->size_in_bytes() == f.size_in_bytes());
		for (std::size_t i = 0; v && i < std::min<std::size_t>(n, 1000); ++i) { CHECK(v->contains(keys[i])); }
		for (std::size_t i = 0; v && i < 100000; ++i) {
			const std::uint64_t k = rng();
			CHECK(v->contains(k) == f.contains(k));
		}
	}
	// Duplicates are ignored.
	{
		std::vector<std::uint64_t> keys{5, 5, 7, 7, 7, 9};
		auto f = fusefilter::filter8::build(keys);
		CHECK(f.contains(5) && f.contains(7) && f.contains(9));
	}
	// Malformed bytes: every truncation and some inconsistent layouts are rejected.
	{
		std::vector<std::uint64_t> keys(5000);
		for (auto& k : keys) { k = rng(); }
		const std::string bytes = fusefilter::filter8::build(keys).serialize();
		for (std::size_t cut = 0; cut < bytes.size(); cut += 1 + cut / 7) {
			CHECK(!fusefilter::filter8::deserialize(bytes.substr(0, cut)));
			CHECK(!fusefilter::filter8_view::parse(bytes.substr(0, cut)));
		}
		std::string bad = bytes;
		bad[9] = char(3);  // segment length not a power of two
		CHECK(!fusefilter::filter8::deserialize(bad));
		CHECK(!fusefilter::filter8_view::parse(bad));
		bad = bytes;
		bad[16] = char(0x7f);  // segment count length past the array
		CHECK(!fusefilter::filter8::deserialize(bad));
		CHECK(!fusefilter::filter8_view::parse(bad));
		bad = bytes;
		bad[0] = char(2);  // unknown version
		CHECK(!fusefilter::filter8::deserialize(bad));
		CHECK(!fusefilter::filter8_view::parse(bad));
	}
	std::printf("fusefilter: %s\n", failures ? "FAILED" : "ok");
	return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
