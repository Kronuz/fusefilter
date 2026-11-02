# Changelog

All notable changes to `fusefilter`. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); the library has no
releases, so entries are dated.

## 2026-11-01

### Changed

- `filter8::build` is 3.9 and 2.9 times faster on the Apple M4 Pro (37.2 to 9.6 ns per key at
  1,000,000 keys, 41.8 to 14.2 at 10,000,000) and 3.9 and 3.2 times on the Intel i9-9980HK
  (107.2 to 27.6, 125.2 to 39.4). It trailed `binary_fuse8_populate` of xor_singleheader by 1.7 to
  2.7 times and now beats it: 1.9 and 1.7 times on the M4 Pro, 1.4 and 1.3 times on the i9 in the
  same A/B, and in the README table (median of 5 runs) 9.7 and 14.3 ns per key against 18.8 and
  24.8 on the M4 Pro, 30 and 40 against 39 and 53 on the i9. Lookups, bits per key (9.04 and 9.02)
  and the false-positive rate (0.39%) are unchanged. The A/B ran in one `quiet strict` hold per
  machine, 15 rounds with an A/A copy of the new build (`docs/perf/comparators.md`).
- The build no longer sorts and deduplicates its keys. It peels them as they come; only when the
  first attempt fails does it look for duplicates, by sorting the hashes of the keys left in the
  2-core and comparing neighbours, and only a duplicate makes it sort and deduplicate the keys and
  start the seeds over. A set without duplicates is never sorted, and `build(std::vector&&)`
  leaves such a set untouched (it used to sort it in place). Every duplicate and no other key
  stays in the core, so the answer is exact, and a set still builds the same bytes whatever its
  order and its duplicates.
- The hashes are grouped in at most 1,024 buckets (there were 65,536, which is 4 MB of live cache
  lines for one write each). The counts are bytes, with a check that they add up to 3n so that a
  slot of 256 keys (257 equal keys) is never read as a slot of one. The peel takes slots first in,
  first out, fetches the lines of the slots 16 entries ahead and of the slots the key 8 entries
  ahead will touch, and leaves a removed key's own slot alone.
- The array of a filter differs from the one the old build made for the same keys (the peel order
  differs); the seed, the shape, the size and the serialized format are the same, and filters
  built before still deserialize and answer as before.
- The working set of a build is 25.3 bytes per key at its peak, filter included, through either
  overload (the old build: 33.5 bytes through the span overload, which copied the keys, and 25.5
  through the vector overload).
- `docs/perf/comparators.md` and the README table carry the new rows. The `-march=native` rows are
  gone: only the default flags are measured.

What the numbers came from, ns per key at 1,000,000 and 10,000,000 keys (median of 9 rounds, M4 Pro
then i9; xor_singleheader in the same runs 18.1 and 24.5, 38.8 and 53.5): the old build 36.8 and
41.8, 106.9 and 124.6; no sort, last in first out peel 20.9 and 26.3, 44.3 and 55.7; first in,
first out with prefetch 12.1 and 17.9, 34.0 and 46.7; with byte counts 11.2 and 15.8, 29.4 and 40.0;
with the lone slot left alone (the build) 10.2 and 14.9, 28.3 and 39.4. A prefetch in the counting
pass was 9% slower at 1,000,000 keys and 5% faster at 10,000,000 on the i9 and changed nothing
on the M4 Pro; one in the assign pass changed nothing. A 16-byte slot holding count and XOR was
slower (15% and 20% on the Linux VM). Filling the buckets in one pass with a spill into the next
bucket (like xor_singleheader) gained 2% to 6% and probes up to every bucket for a key on keys
chosen to fall in one, so it is not used.

### Added

- `test/build_test.cc` (CTest `fusefilter_build`): sets of 0 to 1,000,000 keys, 1% to 50%
  duplicates, every key equal (1 to 1,000,000 keys, with the sizes around 256 and 512 where a byte
  count wraps), structured keys, keys picked to fall in one bucket, and keys picked to fail the
  first seed without being duplicates (including 300 distinct keys on one triple of slots). Each
  is built through both overloads and checked against `test/fusefilter_old.hh` (the build as it
  was, frozen): the same header (seed, shape, size), no false negative, the same bytes in any
  order and as the set without its duplicates; then the false-positive rate (0.33% to 0.45%),
  the working set and that a vector with no duplicates is left alone.
- `tools/mutants.py`: 16 edits of the build; `build_test` fails on each (killed 16 of 16; one,
  every failure sorting the keys, first survived and gained a test).
- `fuzz/fuzz_build.cc`: any key set builds, answers present for each key, and builds the bytes of
  its sorted unique set (4.96 million inputs in 91 seconds under ASan and UBSan).
- `bench/ab_bench.cc`: the frozen build, the current build, the current build again and
  xor_singleheader in a rotating order, with an A/A column.
- `bench/bench.cc` times the reference binary fuse filter (FastFilter/xor_singleheader,
  pinned commit) and two `boost::bloom::filter` sizes beside `filter8`, for 1,000,000 and
  10,000,000 keys, and aborts when a filter misses a present key. They are built only with
  `-DFUSEFILTER_BENCH_COMPARATORS=ON` and are not dependencies of the library.
- `docs/perf/comparators.md`: the method and every row on the Apple M4 Pro and the Intel
  Core i9-9980HK.

## 2026-10-26

### Added

- `filter8`: build from 64-bit keys, `contains`, `serialize`, and `deserialize`;
  `filter8_view` answers from serialized bytes in place.
- `build(std::vector<std::uint64_t>&&)`, which sorts and deduplicates the keys in
  place to hold less working memory during a build.
- `fuzz/fuzz_deserialize.cc`, a libFuzzer target over `deserialize` and
  `filter8_view::parse`; it ran 227 million inputs clean under ASan and UBSan.
- `bench/bench.cc`: build and lookup cost and bits per key for N random keys.
