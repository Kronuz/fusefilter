// fusefilter's build against the frozen version that sorted and deduplicated its keys first
// (test/fusefilter_old.hh): for sets of every shape, duplicates among them, the filter's seed and
// shape are the old build's, no key is missing, the bytes depend on the set alone, the
// false-positive rate is in its band, and the working set and the keys left behind are what the
// header says.
#include "fusefilter.hh"
#include "fusefilter_old.hh"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <numeric>
#include <random>
#include <string>
#include <vector>

static int failures = 0;
#define CHECK(cond) \
	do { \
		if (!(cond)) { std::fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); ++failures; } \
	} while (0)
#define CHECK_SET(cond, name, n) \
	do { \
		if (!(cond)) { std::fprintf(stderr, "%s:%d: CHECK(%s) for %s, %zu keys\n", __FILE__, __LINE__, #cond, name, std::size_t(n)); ++failures; } \
	} while (0)

// Every allocation of the program is counted, so a build's peak working set can be read.
// The replacements are not inlined, so the compiler never sees a malloc behind a new. The
// ThreadSanitizer runtime defines operator new itself, so under it nothing is replaced and the
// working set is not checked.
#if defined(__SANITIZE_THREAD__)
#define FUSEFILTER_TEST_TSAN 1
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define FUSEFILTER_TEST_TSAN 1
#endif
#endif
#if defined(__GNUC__)
#define NOINLINE __attribute__((noinline))
#else
#define NOINLINE
#endif
namespace {
std::size_t live_bytes = 0, peak_bytes = 0;
struct alignas(16) header {
	std::size_t size;
};
}
#ifndef FUSEFILTER_TEST_TSAN
NOINLINE void* operator new(std::size_t n) {
	auto* h = static_cast<header*>(std::malloc(sizeof(header) + n));
	if (!h) { throw std::bad_alloc(); }
	h->size = n;
	live_bytes += n;
	peak_bytes = std::max(peak_bytes, live_bytes);
	return h + 1;
}
NOINLINE void* operator new[](std::size_t n) { return operator new(n); }
NOINLINE void operator delete(void* p) noexcept {
	if (!p) { return; }
	auto* h = static_cast<header*>(p) - 1;
	live_bytes -= h->size;
	std::free(h);
}
NOINLINE void operator delete[](void* p) noexcept { operator delete(p); }
NOINLINE void operator delete(void* p, std::size_t) noexcept { operator delete(p); }
NOINLINE void operator delete[](void* p, std::size_t) noexcept { operator delete(p); }
#endif

using keys_type = std::vector<std::uint64_t>;

static constexpr std::uint64_t first_seed = 0x9e3779b97f4a7c15ull;

// The inverse of detail::mix: the key that hashes to h under `seed`.
static std::uint64_t unmix(std::uint64_t h) {
	h ^= h >> 33;
	h *= 0x9cb4b2f8129337dbull;
	h ^= h >> 33;
	h *= 0x4f74430c22a54005ull;
	h ^= h >> 33;
	return h;
}
static std::uint64_t key_with_hash(std::uint64_t h) { return unmix(h) - first_seed; }

static keys_type random_keys(std::size_t n, std::mt19937_64& rng) {
	keys_type keys(n);
	for (auto& k : keys) { k = rng(); }
	return keys;
}

// `total` keys of which `total - distinct` repeat another, in random order.
static keys_type with_duplicates(std::size_t total, std::size_t distinct, std::mt19937_64& rng) {
	keys_type keys = random_keys(distinct, rng);
	while (keys.size() < total) { keys.push_back(keys[rng() % distinct]); }
	std::shuffle(keys.begin(), keys.end(), rng);
	return keys;
}

static std::string bytes_of(const fusefilter::filter8& f) { return f.serialize(); }

// The checks every set gets. The old build and the new one peel in a different order, so their
// arrays differ, but a set's seed and shape depend only on which seeds peel it, and that is the
// same for both: the header (version, seed, shape, array length) is equal. Then: no false
// negative through either overload, the round trip, and bytes that depend on the set alone (the
// same bytes in any order, and the same as the set without its duplicates).
static void check_set(const keys_type& keys, const char* name) {
	const std::size_t n = keys.size();
	const std::string want = fusefilter_old::filter8::build(keys).serialize();
	const auto f = fusefilter::filter8::build(std::span<const std::uint64_t>(keys));
	const std::string got = bytes_of(f);
	CHECK_SET(got.size() == want.size(), name, n);
	CHECK_SET(got.compare(0, 21, want, 0, 21) == 0, name, n);
	keys_type taken = keys;
	const auto g = fusefilter::filter8::build(std::move(taken));
	CHECK_SET(bytes_of(g) == got, name, n);
	std::size_t missing = 0;
	for (auto k : keys) { missing += !f.contains(k); }
	CHECK_SET(missing == 0, name, n);
	const auto copy = fusefilter::filter8::deserialize(got);
	CHECK_SET(copy.has_value(), name, n);
	for (std::size_t i = 0; copy && i < n; i += 1 + n / 2000) { CHECK_SET(copy->contains(keys[i]), name, n); }
	if (n > 1) {
		keys_type shuffled = keys;
		std::mt19937_64 r(n);
		std::shuffle(shuffled.begin(), shuffled.end(), r);
		CHECK_SET(bytes_of(fusefilter::filter8::build(shuffled)) == got, name, n);
		keys_type unique = keys;
		std::sort(unique.begin(), unique.end());
		unique.erase(std::unique(unique.begin(), unique.end()), unique.end());
		CHECK_SET(bytes_of(fusefilter::filter8::build(unique)) == got, name, n);
	}
}

static void test_sizes() {
	std::mt19937_64 rng(5);
	std::vector<std::size_t> sizes;
	for (std::size_t n = 0; n <= 40; ++n) { sizes.push_back(n); }
	for (std::size_t n : {63u, 64u, 65u, 127u, 128u, 129u, 255u, 256u, 257u, 1000u, 2047u, 2048u, 2049u, 4095u, 4096u, 4097u, 8191u, 8192u, 8193u,
			10000u, 20000u, 50000u, 100000u, 250000u}) {
		sizes.push_back(n);
	}
	for (auto n : sizes) { check_set(random_keys(n, rng), "random"); }
	// Near a segment-count boundary no seed peels the paper's table; the build grows it.
	std::mt19937_64 r(4);
	check_set(random_keys(11509, r), "segment boundary");
}

// Sets in which a share of the entries repeat another, from 1% to 50%, over a range of sizes.
static void test_duplicates() {
	std::mt19937_64 rng(6);
	for (std::size_t total : {10u, 100u, 1000u, 10000u, 100000u}) {
		for (std::size_t percent : {1u, 5u, 10u, 25u, 50u}) {
			const std::size_t distinct = std::max<std::size_t>(1, total - total * percent / 100);
			check_set(with_duplicates(total, distinct, rng), "duplicates");
		}
	}
	// Every key twice, three times, seven times.
	for (std::size_t times : {2u, 3u, 7u}) {
		keys_type distinct = random_keys(1000, rng), keys;
		for (std::size_t i = 0; i < times; ++i) { keys.insert(keys.end(), distinct.begin(), distinct.end()); }
		std::shuffle(keys.begin(), keys.end(), rng);
		check_set(keys, "every key repeated");
	}
	// All keys equal, and a few values with many copies each. A slot counts keys in a byte, so
	// the sizes around 256 and 512 copies are where a count wraps to 0 and to 1.
	for (std::size_t n : {1u, 2u, 3u, 4u, 5u, 100u, 255u, 256u, 257u, 258u, 300u, 512u, 513u, 10000u, 1000000u}) {
		check_set(keys_type(n, 0x1234567890abcdefull), "all equal");
	}
	check_set(keys_type(1000, 0), "all zero");
	check_set(keys_type(1000, ~0ull), "all ones");
	{
		keys_type keys;
		for (int i = 0; i < 500; ++i) { keys.push_back(7), keys.push_back(~0ull); }
		check_set(keys, "two values");
	}
	// One duplicate among many keys, and one key missing from a duplicated run.
	{
		keys_type keys = random_keys(100000, rng);
		keys.push_back(keys[12345]);
		check_set(keys, "one duplicate");
	}
}

// Keys that are not random: structure the hash has to destroy.
static void test_structured() {
	keys_type keys;
	for (std::uint64_t i = 0; i < 100000; ++i) { keys.push_back(i); }
	check_set(keys, "sequential");
	keys.clear();
	for (std::uint64_t i = 0; i < 100000; ++i) { keys.push_back(i << 32); }
	check_set(keys, "high halves only");
	keys.clear();
	for (std::uint64_t i = 0; i < 100000; ++i) { keys.push_back(i << 48 | 0xffff); }
	check_set(keys, "top bits only");
	keys.clear();
	for (std::uint64_t i = 0; i < 50000; ++i) { keys.push_back(i * 0x9e3779b97f4a7c15ull); }
	check_set(keys, "multiples of the golden ratio");
	check_set({0, ~0ull, 1, 1ull << 63, 1ull << 32, 0xffffffffull}, "extremes");
}

// Keys picked by their hash under the first seed, to meet the build's worst cases.
static void test_adversarial() {
	std::mt19937_64 rng(7);
	// Every hash in the first bucket: the counting sort puts all of them in one run.
	keys_type keys;
	for (int i = 0; i < 100000; ++i) { keys.push_back(key_with_hash(rng() >> 10)); }
	check_set(keys, "one bucket");
	// Every hash in the last bucket.
	keys.clear();
	for (int i = 0; i < 100000; ++i) { keys.push_back(key_with_hash(rng() | 0xffc0000000000000ull)); }
	check_set(keys, "last bucket");
	// Twins: a second key whose hash differs in two bits no position or fingerprint reads, so
	// both pick the same three slots with the same fingerprint, as a duplicate would, and
	// the keys are different. The first seed cannot peel them; the next one can.
	for (std::size_t n : {1000u, 5000u}) {
		keys = random_keys(n, rng);
		for (std::size_t i = 0; i < 20; ++i) {
			const std::uint64_t h = fusefilter::detail::mix(keys[i] + first_seed);
			keys.push_back(key_with_hash(h ^ (1ull << 12) ^ (1ull << 28)));
		}
		std::shuffle(keys.begin(), keys.end(), rng);
		check_set(keys, "twins");
		const std::string bytes = fusefilter::filter8::build(keys).serialize();
		std::uint64_t seed = 0;
		for (int i = 0; i < 8; ++i) { seed |= std::uint64_t(std::uint8_t(bytes[1 + i])) << (8 * i); }
		CHECK(seed != first_seed);  // the first attempt failed and the set holds no duplicate
	}
	// Twins and real duplicates together.
	keys = random_keys(2000, rng);
	for (std::size_t i = 0; i < 10; ++i) {
		keys.push_back(key_with_hash(fusefilter::detail::mix(keys[i] + first_seed) ^ (1ull << 12)));
		keys.push_back(keys[100 + i]);
	}
	check_set(keys, "twins and duplicates");
	// 300 keys on one triple of slots with one fingerprint, which no byte count holds. They are
	// all different, so the first seed fails by a wrapped count, not by a duplicate.
	keys = random_keys(2000, rng);
	const std::uint64_t base = fusefilter::detail::mix(keys[0] + first_seed);
	for (std::uint64_t i = 1; i <= 300; ++i) { keys.push_back(key_with_hash(base ^ (i << 8))); }
	check_set(keys, "wrapped counts");
	const std::string bytes = fusefilter::filter8::build(keys).serialize();
	std::uint64_t seed = 0;
	for (int i = 0; i < 8; ++i) { seed |= std::uint64_t(std::uint8_t(bytes[1 + i])) << (8 * i); }
	CHECK(seed != first_seed);
}

// The false-positive rate stays at 1/256, with and without duplicates.
static void test_rate() {
	std::mt19937_64 rng(8);
	const std::size_t probes = 400000;
	for (std::size_t n : {10000u, 100000u, 1000000u}) {
		for (bool duplicates : {false, true}) {
			const keys_type keys = duplicates ? with_duplicates(n + n / 4, n, rng) : random_keys(n, rng);
			const auto f = fusefilter::filter8::build(keys);
			std::size_t fp = 0, missing = 0;
			for (auto k : keys) { missing += !f.contains(k); }
			for (std::size_t i = 0; i < probes; ++i) { fp += f.contains(rng()); }
			const double rate = 100.0 * double(fp) / double(probes), bits = 8.0 * double(f.size_in_bytes()) / double(n);
			std::printf("n=%zu%s: %.3f%% false positives, %.2f bits/key\n", n, duplicates ? " with duplicates" : "", rate, bits);
			CHECK(missing == 0);
			CHECK(rate > 0.33 && rate < 0.45);
			CHECK(bits < (n >= 100000 ? 9.6 : 10.5));
		}
	}
}

// What build leaves of the keys it takes over, and what it holds while it builds.
static void test_keys_and_memory() {
	std::mt19937_64 rng(9);
	{
		keys_type keys = random_keys(10000, rng);
		const keys_type before = keys;
		fusefilter::filter8::build(std::move(keys));
		CHECK(keys == before);  // no duplicates: not touched
	}
	{
		// A set that fails its first seed without a duplicate (twins) is not touched either.
		keys_type keys = random_keys(1000, rng);
		for (std::size_t i = 0; i < 20; ++i) {
			keys.push_back(key_with_hash(fusefilter::detail::mix(keys[i] + first_seed) ^ (1ull << 12) ^ (1ull << 28)));
		}
		const keys_type before = keys;
		fusefilter::filter8::build(std::move(keys));
		CHECK(keys == before);
	}
	{
		keys_type keys = with_duplicates(10000, 7000, rng);
		keys_type sorted = keys;
		std::sort(sorted.begin(), sorted.end());
		sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
		fusefilter::filter8::build(std::move(keys));
		CHECK(keys == sorted);  // duplicates: sorted and deduplicated in place
	}
#ifdef FUSEFILTER_TEST_TSAN
	return;
#endif
	const std::size_t n = 200000;
	const keys_type keys = random_keys(n, rng);
	auto peak = [&](auto&& build) {
		const std::size_t base = live_bytes;
		peak_bytes = live_bytes;
		{ auto f = build(); (void)f; }
		return peak_bytes - base;
	};
	const std::size_t by_span = peak([&] { return fusefilter::filter8::build(std::span<const std::uint64_t>(keys)); });
	const std::size_t by_vector = peak([&] { return fusefilter::filter8::build(keys_type(keys)); }) - n * 8;  // less the keys it was handed
	const std::size_t old_span = peak([&] { return fusefilter_old::filter8::build(std::span<const std::uint64_t>(keys)); });
	const std::size_t old_vector = peak([&] { return fusefilter_old::filter8::build(keys_type(keys)); }) - n * 8;
	std::printf("peak working set per key: %.1f bytes by span, %.1f by vector; the version that sorted first, %.1f by span, %.1f by vector\n", double(by_span) / n,
		double(by_vector) / n, double(old_span) / n, double(old_vector) / n);
	CHECK(by_span < 27 * n);
	CHECK(by_vector < 27 * n);
	CHECK(by_span < old_span);
	CHECK(by_vector * 100 < old_vector * 105);  // about the same as the old in-place build, which kept no copy either
}

int main() {
	// The inverse used to pick keys by their hash.
	{
		std::mt19937_64 rng(1);
		for (int i = 0; i < 1000; ++i) {
			const std::uint64_t h = rng();
			CHECK(fusefilter::detail::mix(key_with_hash(h) + first_seed) == h);
		}
	}
	test_sizes();
	test_duplicates();
	test_structured();
	test_adversarial();
	test_rate();
	test_keys_and_memory();
	std::printf("fusefilter build: %s\n", failures ? "FAILED" : "ok");
	return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
