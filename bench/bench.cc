// Build and lookup cost for N random keys (argv[1], default 10,000,000): ns per key to
// build, ns per present and absent lookup, bits per key.
#include "fusefilter.hh"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>

int main(int argc, char** argv) {
	const std::size_t n = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 10000000;
	std::mt19937_64 rng(1);
	std::vector<std::uint64_t> keys(n), absent(n);
	for (auto& k : keys) { k = rng(); }
	for (auto& k : absent) { k = rng(); }
	using clock = std::chrono::steady_clock;
	auto t0 = clock::now();
	auto f = fusefilter::filter8::build(keys);
	auto t1 = clock::now();
	std::size_t hits = 0;
	for (auto k : keys) { hits += f.contains(k); }
	auto t2 = clock::now();
	std::size_t fp = 0;
	for (auto k : absent) { fp += f.contains(k); }
	auto t3 = clock::now();
	auto ns = [&](auto a, auto b) { return std::chrono::duration<double, std::nano>(b - a).count() / double(n); };
	std::printf("n=%zu: build %.1f ns/key, present %.1f ns, absent %.1f ns, %.2f bits/key, %.3f%% false positives (hits %zu)\n", n, ns(t0, t1), ns(t1, t2),
		ns(t2, t3), 8.0 * double(f.size_in_bytes()) / double(n), 100.0 * double(fp) / double(n), hits);
}
