# Comparators: method and every row

`fusefilter_bench` builds `fusefilter::filter8` and three comparators from N random 64-bit keys and times each one. The README's table is the rows below; this page adds the spread and the method. `fusefilter_ab_bench` times the builds against each other and is the last section.

## What is compared

| Name in the tables | What it is | Version |
|---|---|---|
| fusefilter filter8 | `fusefilter::filter8` of this repository | the commit that holds this page |
| xor_singleheader binary_fuse8_t | `binary_fuse8_t` of [FastFilter/xor_singleheader](https://github.com/FastFilter/xor_singleheader), a C header: the implementation by Thomas Mueller Graf and Daniel Lemire, who designed binary fuse filters ([arXiv:2201.01174](https://arxiv.org/abs/2201.01174)) | commit `5426b7d84994ecf60e596fde8ae70bc1ae7a9720` (fetched by CMake) |
| boost::bloom k=6, same bits | `boost::bloom::filter<std::uint64_t, 6>` ([Boost.Bloom](https://www.boost.org/doc/libs/release/libs/bloom/)), the default subfilter (a classic Bloom filter, one bit per probe), sized to the bits per key that fusefilter uses at that N (9.04 at 1,000,000 keys, 9.02 at 10,000,000); 6 is the integer nearest ln 2 times 9 | Boost 1.92.0 (Homebrew, found) |
| boost::bloom k=8, same rate | `boost::bloom::filter<std::uint64_t, 8>` constructed with `(N, 0.0039)`, so Boost picks the size (11.54 bits per key) for the 0.39% false-positive rate that fusefilter reaches | Boost 1.92.0 |

[FastFilter/fastfilter_cpp](https://github.com/FastFilter/fastfilter_cpp) is not in the bench: xor_singleheader is the authors' own implementation of the same filter, so a second copy of it adds a template wrapper and nothing else.

## Method

- Keys: `std::mt19937_64` seeded with 1, N keys for the set and N further keys that are absent (a collision would need two 64-bit draws to agree). The keys are distinct, so no build pays for a duplicate.
- Build: ns per key, best of 5 builds at 1,000,000 keys and of 3 at 10,000,000. Every build starts from a fresh copy of the keys, made outside the timer, because `filter8::build(std::vector&&)` sorts a set that holds duplicates in place and xor_singleheader's retry path reorders its input. The Bloom rows insert the keys one by one into a filter sized up front.
- Lookup: ns per call over all N present keys and over all N absent keys, best of 3 passes. The queries are independent, so the number is throughput of a pipelined loop, not the latency of one dependent call. A compiler barrier before each pass makes the loop run on every pass.
- Bits per key: the filter's bytes times 8 over N (`size_in_bytes()`, `binary_fuse8_size_in_bytes()`, `capacity() / 8` for Boost).
- False positives: the share of the N absent keys reported present.
- Checks: every filter must report all N present keys present, and the bench aborts with a message when one does not (a false negative). The filters hash differently, so their answers on absent keys are not equal; the false-positive rate is what is compared. fusefilter and xor_singleheader both measure 0.39% (they are the same construction; 1/256 is 0.39%), Boost's k=8 filter is sized to it, and k=6 at 9 bits per key is 1.28% to 1.32%.
- Runs: on each machine the bench was configured, built and run 5 times one after another inside one `quiet strict` hold, which keeps other timing jobs off the machine. The tables give the median of the runs and, in brackets, the smallest and largest. The spread is the A/A figure: fusefilter and xor_singleheader have the same lookup loop over the same layout and differ by less than that spread at both sizes on both machines.

## Machines and commands

- Apple M4 Pro, macOS 26, Apple clang 17.0.0 (clang-1700.3.19.1), `-O3 -DNDEBUG` (CMake Release), arm64 binaries.
- Intel Core i9-9980HK (8 cores, 2.4 GHz base), macOS 15, Apple clang 17.0.0 (clang-1700.6.4.2), `-O3 -DNDEBUG`.

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release -DFUSEFILTER_BENCH_COMPARATORS=ON
cmake --build build --target fusefilter_bench fusefilter_ab_bench
build/fusefilter_bench 1000000 10000000
build/fusefilter_ab_bench -r 15 1000000 10000000
```

`FUSEFILTER_BENCH_COMPARATORS` needs CMake 3.18, Boost 1.89 or later with its CMake package (Homebrew `boost`), and network access for the fetch of the xor_singleheader header. It is off by default and the `fusefilter` target never depends on it; without it the bench times `filter8` alone.

## Rows

Build in ns per key (lower is faster), lookup in ns per call (lower is faster).

### Apple M4 Pro (5 runs)

| Keys | Filter | Build (ns/key) | Present (ns) | Absent (ns) | Bits per key | False positives |
|---:|---|---:|---:|---:|---:|---:|
| 1,000,000 | fusefilter filter8 | 9.7 (9.2 to 10.1) | 1.6 (1.5 to 1.6) | 1.5 (1.5 to 1.6) | 9.04 | 0.39% |
| 1,000,000 | xor_singleheader binary_fuse8_t | 18.8 (17.8 to 20.2) | 1.5 (1.5 to 1.6) | 1.6 (1.5 to 1.6) | 9.04 | 0.39% |
| 1,000,000 | boost::bloom k=6, same bits | 4.9 (4.8 to 5.0) | 3.1 (3.1 to 3.2) | 8.9 (8.6 to 9.0) | 9.04 | 1.28% |
| 1,000,000 | boost::bloom k=8, same rate | 6.2 (6.0 to 6.3) | 4.2 (4.1 to 4.2) | 9.6 (9.3 to 9.6) | 11.54 | 0.39% |
| 10,000,000 | fusefilter filter8 | 14.3 (14.2 to 15.2) | 2.0 (2.0 to 2.1) | 2.0 (2.0 to 2.0) | 9.02 | 0.39% |
| 10,000,000 | xor_singleheader binary_fuse8_t | 24.8 (24.7 to 25.7) | 2.0 (2.0 to 2.1) | 2.0 (2.0 to 2.1) | 9.02 | 0.39% |
| 10,000,000 | boost::bloom k=6, same bits | 5.9 (5.8 to 5.9) | 4.1 (4.0 to 4.1) | 9.9 (9.9 to 10.2) | 9.02 | 1.32% |
| 10,000,000 | boost::bloom k=8, same rate | 7.0 (6.9 to 7.1) | 5.4 (5.2 to 5.5) | 11.2 (10.9 to 11.9) | 11.54 | 0.39% |

### Intel Core i9-9980HK (5 runs)

| Keys | Filter | Build (ns/key) | Present (ns) | Absent (ns) | Bits per key | False positives |
|---:|---|---:|---:|---:|---:|---:|
| 1,000,000 | fusefilter filter8 | 29.5 (27.1 to 32.8) | 5.8 (5.5 to 5.9) | 5.6 (5.2 to 5.8) | 9.04 | 0.39% |
| 1,000,000 | xor_singleheader binary_fuse8_t | 38.6 (37.9 to 39.4) | 5.7 (5.5 to 5.8) | 5.6 (5.6 to 5.7) | 9.04 | 0.39% |
| 1,000,000 | boost::bloom k=6, same bits | 12.6 (11.8 to 12.9) | 10.5 (10.2 to 11.0) | 14.7 (14.6 to 15.2) | 9.04 | 1.28% |
| 1,000,000 | boost::bloom k=8, same rate | 15.1 (14.7 to 15.5) | 12.8 (12.6 to 13.5) | 14.2 (14.0 to 14.3) | 11.54 | 0.39% |
| 10,000,000 | fusefilter filter8 | 39.8 (39.7 to 40.5) | 12.8 (12.5 to 13.1) | 12.8 (12.6 to 15.4) | 9.02 | 0.39% |
| 10,000,000 | xor_singleheader binary_fuse8_t | 52.8 (51.1 to 53.1) | 12.5 (11.4 to 13.6) | 12.4 (12.2 to 13.4) | 9.02 | 0.39% |
| 10,000,000 | boost::bloom k=6, same bits | 25.0 (23.1 to 27.1) | 20.6 (19.0 to 23.2) | 19.3 (17.6 to 19.7) | 9.02 | 1.32% |
| 10,000,000 | boost::bloom k=8, same rate | 46.4 (45.8 to 47.2) | 37.7 (37.3 to 38.5) | 23.1 (22.8 to 23.4) | 11.54 | 0.39% |

## Reading the rows

- Lookup: fusefilter and xor_singleheader are the same speed within the spread, since they read the same three slots. Boost's k=8 filter, which reaches the same false-positive rate, is 2.2 to 2.9 times slower on a present key (1.8 to 6.2 times on an absent key).
- Space: fusefilter and xor_singleheader take 9.02 to 9.04 bits per key; the Bloom filter takes 11.54 bits per key for the same 0.39%, and 9.04 bits per key for 1.28% to 1.32%.
- Build: fusefilter builds 1.3 to 1.9 times faster than xor_singleheader, and a Bloom filter 1.6 to 2.4 times faster than fusefilter; only `Bloom k=8` at 10,000,000 keys on the i9 is slower (1.2 times). A Bloom insert is one pass over the keys with no peeling; the build is paid once per immutable set.

## Build A/B

`fusefilter_ab_bench` builds the keys of the same run with `test/fusefilter_old.hh` (a frozen build that sorts and deduplicates its keys first), the current `filter8`, the current `filter8` again (the A/A that shows the noise), and xor_singleheader. The four builds of a round run one after another in a rotating order, so a slow stretch of the machine falls on each alike; 15 rounds, ns per key as median (min to max). It ran in the same `quiet strict` hold as the table bench.

| Machine | Keys | frozen sorting build | filter8 | filter8, A/A | xor_singleheader |
|---|---:|---:|---:|---:|---:|
| Apple M4 Pro | 1,000,000 | 37.2 (36.8 to 40.9) | 9.6 (9.3 to 10.1) | 9.4 (9.3 to 10.1) | 18.3 (17.9 to 19.6) |
| Apple M4 Pro | 10,000,000 | 41.8 (41.6 to 43.3) | 14.2 (14.1 to 14.8) | 14.3 (14.1 to 14.5) | 24.5 (24.2 to 25.7) |
| Intel Core i9-9980HK | 1,000,000 | 107.2 (104.5 to 113.5) | 27.6 (25.5 to 34.5) | 27.5 (25.6 to 32.4) | 39.5 (37.5 to 44.3) |
| Intel Core i9-9980HK | 10,000,000 | 125.2 (123.2 to 135.9) | 39.4 (38.6 to 42.1) | 39.2 (38.2 to 40.2) | 52.7 (51.5 to 54.7) |

The ratios of medians: the frozen build over `filter8` is 3.9 and 2.9 on the M4 Pro and 3.9 and 3.2 on the i9 (1,000,000 and 10,000,000 keys); xor_singleheader over `filter8` is 1.9 and 1.7 on the M4 Pro and 1.4 and 1.3 on the i9; `filter8` over its A/A copy is 0.99 to 1.02. The false-positive rate of the four is 0.387% to 0.391% in every run. The filter bytes of `filter8` differ from the frozen build's (the peel order differs), and the seed, the shape and the size are the same.
