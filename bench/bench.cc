// Build and lookup cost for N random 64-bit keys (argv: one or more N, default 1,000,000 and
// 10,000,000): ns per key to build, ns per present and absent lookup, bits per key, and the
// false-positive rate over N absent keys, for fusefilter and the comparators. Every filter must
// report every present key as present; the bench aborts when one does not.
//   fusefilter_bench [N...]
// With -DFUSEFILTER_BENCH_COMPARATORS=ON it also times binary_fuse8_t of FastFilter/xor_singleheader
// (Graf and Lemire's own implementation) and two boost::bloom::filter sizes.
#include "fusefilter.hh"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <vector>

#ifdef FUSEFILTER_BENCH_REFERENCE
#include "binaryfusefilter.h"
#endif
#ifdef FUSEFILTER_BENCH_BOOST
#include <boost/bloom/filter.hpp>
#endif

using clock_type = std::chrono::steady_clock;

static double seconds_since(clock_type::time_point t) { return std::chrono::duration<double>(clock_type::now() - t).count(); }

struct row {
	std::string name;
	double build_ns = 0, present_ns = 1e300, absent_ns = 1e300, bits = 0, fp_pct = 0;
};

// Builds `reps` times from a fresh copy of the keys (the copy is not timed, because the
// builders reorder their input) and returns the best ns per key and the last filter.
template <typename Make>
static auto timed_build(const std::vector<std::uint64_t>& keys, int reps, double& best_ns, Make&& make) {
	best_ns = 1e300;
	decltype(make(std::vector<std::uint64_t>{})) built{};
	for (int i = 0; i < reps; ++i) {
		std::vector<std::uint64_t> copy(keys);
		auto t = clock_type::now();
		built = make(std::move(copy));
		best_ns = std::min(best_ns, seconds_since(t) * 1e9 / double(keys.size()));
	}
	return built;
}

// Best of three passes of ns per lookup; returns how many keys the filter reported present.
template <typename Contains>
static std::size_t timed_lookup(const std::vector<std::uint64_t>& queries, double& best_ns, Contains&& contains) {
	std::size_t hits = 0;
	for (int pass = 0; pass < 3; ++pass) {
		hits = 0;
		asm volatile("" ::: "memory");  // a pure lookup loop must run on every pass
		auto t = clock_type::now();
		for (auto k : queries) { hits += contains(k); }
		asm volatile("" : "+r"(hits)::"memory");
		best_ns = std::min(best_ns, seconds_since(t) * 1e9 / double(queries.size()));
	}
	return hits;
}

// Times lookups of present and absent keys; aborts on a false negative.
template <typename Contains>
static void measure(row& r, std::size_t bytes, const std::vector<std::uint64_t>& keys, const std::vector<std::uint64_t>& absent, Contains&& contains) {
	std::size_t hits = timed_lookup(keys, r.present_ns, contains);
	if (hits != keys.size()) {
		std::fprintf(stderr, "FALSE NEGATIVE: %s reported %zu of %zu present keys present\n", r.name.c_str(), hits, keys.size());
		std::abort();
	}
	std::size_t fp = timed_lookup(absent, r.absent_ns, contains);
	r.bits = 8.0 * double(bytes) / double(keys.size());
	r.fp_pct = 100.0 * double(fp) / double(absent.size());
}

static void run(std::size_t n) {
	std::mt19937_64 rng(1);
	std::vector<std::uint64_t> keys(n), absent(n);
	for (auto& k : keys) { k = rng(); }
	for (auto& k : absent) { k = rng(); }
	const int reps = n <= 1000000 ? 5 : 3;
	std::vector<row> rows;
	double fuse_bits = 0;

	{
		row r{"fusefilter filter8"};
		auto f = timed_build(keys, reps, r.build_ns, [](std::vector<std::uint64_t>&& v) { return fusefilter::filter8::build(std::move(v)); });
		measure(r, f.size_in_bytes(), keys, absent, [&](std::uint64_t k) { return f.contains(k); });
		fuse_bits = r.bits;
		rows.push_back(r);
	}
#ifdef FUSEFILTER_BENCH_REFERENCE
	{
		row r{"xor_singleheader binary_fuse8_t"};
		struct holder {
			binary_fuse8_t f{};
			holder() = default;
			holder(holder&& o) noexcept : f(o.f) { o.f = binary_fuse8_t{}; }
			holder& operator=(holder&& o) noexcept { std::swap(f, o.f); return *this; }
			~holder() { if (f.Fingerprints) { binary_fuse8_free(&f); } }
		};
		auto h = timed_build(keys, reps, r.build_ns, [&](std::vector<std::uint64_t>&& v) {
			holder x;
			if (!binary_fuse8_allocate(std::uint32_t(v.size()), &x.f) || !binary_fuse8_populate(v.data(), std::uint32_t(v.size()), &x.f)) {
				std::fprintf(stderr, "binary_fuse8 build failed\n");
				std::abort();
			}
			return x;
		});
		measure(r, binary_fuse8_size_in_bytes(&h.f), keys, absent, [&](std::uint64_t k) { return binary_fuse8_contain(k, &h.f); });
		rows.push_back(r);
	}
#endif
#ifdef FUSEFILTER_BENCH_BOOST
	{
		// The same bits per key as fusefilter; the optimal number of probes is ln 2 times it.
		using bloom6 = boost::bloom::filter<std::uint64_t, 6>;
		row r{"boost::bloom k=6, same bits"};
		auto b = timed_build(keys, reps, r.build_ns, [&](std::vector<std::uint64_t>&& v) {
			bloom6 f(std::size_t(fuse_bits * double(v.size())));
			for (auto k : v) { f.insert(k); }
			return f;
		});
		measure(r, b.capacity() / 8, keys, absent, [&](std::uint64_t k) { return b.may_contain(k); });
		rows.push_back(r);
	}
	{
		// Sized by boost for a 0.39% false-positive rate, which is what fusefilter reaches.
		using bloom8 = boost::bloom::filter<std::uint64_t, 8>;
		row r{"boost::bloom k=8, same rate"};
		auto b = timed_build(keys, reps, r.build_ns, [&](std::vector<std::uint64_t>&& v) {
			bloom8 f(v.size(), 0.0039);
			for (auto k : v) { f.insert(k); }
			return f;
		});
		measure(r, b.capacity() / 8, keys, absent, [&](std::uint64_t k) { return b.may_contain(k); });
		rows.push_back(r);
	}
#endif
	(void)fuse_bits;

	std::printf("n=%zu (best of %d builds, best of 3 lookup passes)\n", n, reps);
	std::printf("%-30s %10s %14s %14s %10s %8s\n", "filter", "build ns/k", "present ns", "absent ns", "bits/key", "fp %");
	for (const auto& r : rows) {
		std::printf("%-30s %10.1f %14.2f %14.2f %10.2f %8.3f\n", r.name.c_str(), r.build_ns, r.present_ns, r.absent_ns, r.bits, r.fp_pct);
	}
	std::printf("\n");
}

int main(int argc, char** argv) {
	if (argc < 2) {
		run(1000000);
		run(10000000);
	}
	for (int i = 1; i < argc; ++i) { run(std::strtoull(argv[i], nullptr, 10)); }
}
