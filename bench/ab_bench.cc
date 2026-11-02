// A/B of the builds: fusefilter's current filter8 against the frozen copy of the version that
// sorted its keys first (test/fusefilter_old.hh), a second copy of the current one (the A/A
// that shows the noise of the machine), and with -DFUSEFILTER_BENCH_COMPARATORS=ON
// binary_fuse8_t of xor_singleheader. The builds of one round run one after another in a rotating
// order, so a slow stretch of the machine falls on every variant alike; each ns/key is over
// the rounds' median and (min to max). Lookups are timed on the filters of the last round.
//   fusefilter_ab_bench [-r rounds] [N...]     (default 15 rounds, N = 1,000,000 and 10,000,000)
#include "fusefilter.hh"
#include "fusefilter_old.hh"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <vector>

#ifdef FUSEFILTER_BENCH_REFERENCE
#include "binaryfusefilter.h"
#endif

using clock_type = std::chrono::steady_clock;
using keys_type = std::vector<std::uint64_t>;

static double seconds_since(clock_type::time_point t) { return std::chrono::duration<double>(clock_type::now() - t).count(); }

struct variant {
	std::string name;
	std::function<void(keys_type&&)> build;
	std::function<std::size_t(const keys_type&)> hits;  // keys the built filter reports present
	std::function<std::size_t()> bytes;
	std::function<std::string()> serialized;  // empty when the variant has no serialization
	std::vector<double> build_ns;
	double present_ns = 1e300, absent_ns = 1e300, bits = 0, fp_pct = 0;
};

template <class F, class Build>
static variant make_variant(std::string name, Build build_fn) {
	auto f = std::make_shared<std::optional<F>>();
	variant v;
	v.name = std::move(name);
	v.build = [f, build_fn](keys_type&& keys) { f->reset(); f->emplace(build_fn(std::move(keys))); };
	v.hits = [f](const keys_type& q) {
		std::size_t hits = 0;
		for (auto k : q) { hits += (*f)->contains(k); }
		asm volatile("" : "+r"(hits)::"memory");
		return hits;
	};
	v.bytes = [f] { return (*f)->size_in_bytes(); };
	v.serialized = [f] { return (*f)->serialize(); };
	return v;
}

#ifdef FUSEFILTER_BENCH_REFERENCE
struct reference_filter {
	binary_fuse8_t f{};
	reference_filter() = default;
	reference_filter(reference_filter&& o) noexcept : f(o.f) { o.f = binary_fuse8_t{}; }
	reference_filter& operator=(reference_filter&& o) noexcept { std::swap(f, o.f); return *this; }
	~reference_filter() { if (f.Fingerprints) { binary_fuse8_free(&f); } }
	bool contains(std::uint64_t k) const { return binary_fuse8_contain(k, &f); }
	std::size_t size_in_bytes() const { return binary_fuse8_size_in_bytes(&f); }
	std::string serialize() const { return {}; }
	static reference_filter build(keys_type&& keys) {
		reference_filter x;
		if (!binary_fuse8_allocate(std::uint32_t(keys.size()), &x.f) || !binary_fuse8_populate(keys.data(), std::uint32_t(keys.size()), &x.f)) {
			std::fprintf(stderr, "binary_fuse8 build failed\n");
			std::abort();
		}
		return x;
	}
};
#endif

static double median(std::vector<double> v) {
	std::sort(v.begin(), v.end());
	return v[v.size() / 2];
}

static void run(std::size_t n, int rounds) {
	std::mt19937_64 rng(1);
	keys_type keys(n), absent(n);
	for (auto& k : keys) { k = rng(); }
	for (auto& k : absent) { k = rng(); }

	std::vector<variant> vs;
	vs.push_back(make_variant<fusefilter_old::filter8>("old (77ab0a7)", [](keys_type&& k) { return fusefilter_old::filter8::build(std::move(k)); }));
	vs.push_back(make_variant<fusefilter::filter8>("new", [](keys_type&& k) { return fusefilter::filter8::build(std::move(k)); }));
	vs.push_back(make_variant<fusefilter::filter8>("new (A/A)", [](keys_type&& k) { return fusefilter::filter8::build(std::move(k)); }));
#ifdef FUSEFILTER_BENCH_REFERENCE
	vs.push_back(make_variant<reference_filter>("xor_singleheader", [](keys_type&& k) { return reference_filter::build(std::move(k)); }));
#endif

	for (int round = 0; round < rounds; ++round) {
		for (std::size_t j = 0; j < vs.size(); ++j) {
			variant& v = vs[(j + std::size_t(round)) % vs.size()];
			keys_type copy(keys);  // the builders reorder their input; the copy is not timed
			const auto t = clock_type::now();
			v.build(std::move(copy));
			v.build_ns.push_back(seconds_since(t) * 1e9 / double(n));
		}
	}
	for (int pass = 0; pass < 5; ++pass) {
		for (auto& v : vs) {
			asm volatile("" ::: "memory");
			auto t = clock_type::now();
			const std::size_t present = v.hits(keys);
			v.present_ns = std::min(v.present_ns, seconds_since(t) * 1e9 / double(n));
			if (present != n) {
				std::fprintf(stderr, "FALSE NEGATIVE: %s reported %zu of %zu present keys present\n", v.name.c_str(), present, n);
				std::abort();
			}
			t = clock_type::now();
			const std::size_t fp = v.hits(absent);
			v.absent_ns = std::min(v.absent_ns, seconds_since(t) * 1e9 / double(n));
			v.fp_pct = 100.0 * double(fp) / double(n);
		}
	}
	std::printf("n=%zu, %d rounds, build ns/key: median (min to max)\n", n, rounds);
	std::printf("%-16s %-28s %8s %8s %9s %8s\n", "variant", "build", "present", "absent", "bits/key", "fp %");
	for (auto& v : vs) {
		v.bits = 8.0 * double(v.bytes()) / double(n);
		char build[64];
		std::snprintf(build, sizeof build, "%.1f (%.1f to %.1f)", median(v.build_ns), *std::min_element(v.build_ns.begin(), v.build_ns.end()), *std::max_element(v.build_ns.begin(), v.build_ns.end()));
		std::printf("%-16s %-28s %8.2f %8.2f %9.2f %8.3f\n", v.name.c_str(), build, v.present_ns, v.absent_ns, v.bits, v.fp_pct);
	}
	const double old_med = median(vs[0].build_ns), new_med = median(vs[1].build_ns);
	std::printf("old / new = %.2f, new / new (A/A) = %.2f", old_med / new_med, new_med / median(vs[2].build_ns));
#ifdef FUSEFILTER_BENCH_REFERENCE
	std::printf(", xor_singleheader / new = %.2f", median(vs[3].build_ns) / new_med);
#endif
	std::printf("; filter bytes %s the old ones\n\n", vs[0].serialized() == vs[1].serialized() ? "equal" : "differ from");
}

int main(int argc, char** argv) {
	int rounds = 15;
	std::vector<std::size_t> sizes;
	for (int i = 1; i < argc; ++i) {
		if (!std::strcmp(argv[i], "-r") && i + 1 < argc) { rounds = std::atoi(argv[++i]); }
		else { sizes.push_back(std::strtoull(argv[i], nullptr, 10)); }
	}
	if (sizes.empty()) { sizes = {1000000, 10000000}; }
	for (auto n : sizes) { run(n, rounds); }
}
