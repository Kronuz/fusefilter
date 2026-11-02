# AGENTS.md — fusefilter

## Repo map

```
fusefilter.hh             filter8: build, contains, serialize, deserialize; filter8_view: the same
                          lookups over serialized bytes in place (detail::layout is shared).
test/test.cc              No false negatives, rate and size, duplicates, round trip, malformed bytes.
test/build_test.cc        The build over sets of every shape against test/fusefilter_old.hh: seed and shape
                          equal, no false negative, bytes that depend on the set alone, rate, working set,
                          keys left alone. CTest `fusefilter_build`.
test/fusefilter_old.hh    The build that sorted its keys first, as of 77ab0a7, in namespace fusefilter_old.
                          Never edit it.
tools/mutants.py          16 edits of the build; build_test must fail on each (`CXX=clang++ tools/mutants.py`).
fuzz/fuzz_deserialize.cc  libFuzzer: arbitrary bytes deserialize safely or not at all, and the
                          view accepts and answers exactly as the filter does.
fuzz/fuzz_build.cc        libFuzzer: any key set builds, has no false negative and builds the bytes of
                          its sorted unique set.
bench/bench.cc            Build and lookup cost, bits per key, against xor_singleheader and Bloom filters.
bench/ab_bench.cc         Builds of test/fusefilter_old.hh, the current header, the current header again (A/A)
                          and xor_singleheader in a rotating order (`fusefilter_ab_bench`).
docs/perf/comparators.md  The bench's method and every row on both machines.
docs/fusefilter.d2     README diagram; render: d2 --layout elk --sketch --theme 302 docs/fusefilter.d2 docs/fusefilter.svg
```

## Invariants

- No false negatives: every built key answers true.
- A set builds the same bytes whatever its order and its duplicates. The build does not look for
  duplicates before it peels: duplicates make every peel fail, and only the first failure looks (the
  keys left in the 2-core, sorted by hash). After a duplicate it sorts and deduplicates the keys and
  starts the seeds over; without one it goes on to the next seed. Keep both, and keep the keys of a
  `build(std::vector&&)` untouched unless they hold a duplicate.
- Counts are bytes. The sum of all counts is 3n unless a slot wrapped (256 or more keys); a wrapped
  count fails the attempt and has_duplicates then sorts every hash. Without the check a slot with 257
  equal keys looks like a slot with one.
- The peel leaves a removed key's own slot as it is, so a removed key has a slot with a count of 1:
  has_duplicates reads the core as the keys whose three slots all hold two or more.
- The queue holds each slot at most once (`slots + 3` entries); the prefetch distances are tuned on
  the M4 Pro and the i9, and change only speed.
- deserialize and filter8_view::parse accept only layouts whose three positions stay inside
  the array; both go through detail::layout::parse, the one validation.
- The serialized format carries a version byte; change it with any layout change.

## Verify a change

```sh
cmake -S . -B build && cmake --build build && ctest --test-dir build
CXX=clang++ tools/mutants.py
```

Compare builds with `fusefilter_ab_bench` on a quiet machine (`-DFUSEFILTER_BENCH_COMPARATORS=ON`
adds xor_singleheader); its A/A row is the noise.
