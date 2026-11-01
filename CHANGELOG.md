# Changelog

All notable changes to `fusefilter`. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); the library has no
releases, so entries are dated.

## 2026-10-26

### Added

- `filter8`: build from 64-bit keys, `contains`, `serialize`, and `deserialize`;
  `filter8_view` answers from serialized bytes in place.
- `build(std::vector<std::uint64_t>&&)`, which sorts and deduplicates the keys in
  place to hold less working memory during a build.
- `fuzz/fuzz_deserialize.cc`, a libFuzzer target over `deserialize` and
  `filter8_view::parse`; it ran 227 million inputs clean under ASan and UBSan.
- `bench/bench.cc`: build and lookup cost and bits per key for N random keys.
