# AGENTS.md — fusefilter

## Repo map

```
fusefilter.hh             filter8: build, contains, serialize, deserialize; filter8_view: the same
                          lookups over serialized bytes in place (detail::layout is shared).
test/test.cc              No false negatives, rate and size, duplicates, round trip, malformed bytes.
fuzz/fuzz_deserialize.cc  libFuzzer: arbitrary bytes deserialize safely or not at all, and the
                          view accepts and answers exactly as the filter does.
bench/bench.cc            Build and lookup cost, bits per key.
docs/fusefilter.d2     README diagram; render: d2 --layout elk --sketch --theme 302 docs/fusefilter.d2 docs/fusefilter.svg
```

## Invariants

- No false negatives: every built key answers true.
- deserialize and filter8_view::parse accept only layouts whose three positions stay inside
  the array; both go through detail::layout::parse, the one validation.
- The serialized format carries a version byte; change it with any layout change.

## Verify a change

```sh
cmake -S . -B build && cmake --build build && ctest --test-dir build
```
