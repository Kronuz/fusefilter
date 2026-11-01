# Comparators: method and every row

`fusefilter_bench` builds `fusefilter::filter8` and three comparators from N random 64-bit keys and times each one. The README's table is the default-flag rows below; this page adds the spread, the other flag set and the method.

## What is compared

| Name in the tables | What it is | Version |
|---|---|---|
| fusefilter filter8 | `fusefilter::filter8` of this repository | the commit that holds this page |
| binary_fuse8_t (reference) | `binary_fuse8_t` of the reference implementation of binary fuse filters, [FastFilter/xor_singleheader](https://github.com/FastFilter/xor_singleheader), a C header | commit `5426b7d84994ecf60e596fde8ae70bc1ae7a9720` (fetched by CMake) |
| boost::bloom k=6, same bits | `boost::bloom::filter<std::uint64_t, 6>` ([Boost.Bloom](https://www.boost.org/doc/libs/release/libs/bloom/)), the default subfilter (a classic Bloom filter, one bit per probe), sized to the bits per key that fusefilter uses at that N (9.04 at 1,000,000 keys, 9.02 at 10,000,000); 6 is the integer nearest ln 2 times 9 | Boost 1.92.0 (Homebrew, found) |
| boost::bloom k=8, same rate | `boost::bloom::filter<std::uint64_t, 8>` constructed with `(N, 0.0039)`, so Boost picks the size (11.54 bits per key) for the 0.39% false-positive rate that fusefilter reaches | Boost 1.92.0 |

[FastFilter/fastfilter_cpp](https://github.com/FastFilter/fastfilter_cpp) is not in the bench: the C header is the reference of the same filter, so a second copy of it adds a template wrapper and nothing else.

## Method

- Keys: `std::mt19937_64` seeded with 1, N keys for the set and N further keys that are absent (a collision would need two 64-bit draws to agree).
- Build: ns per key, best of 5 builds at 1,000,000 keys and of 3 at 10,000,000. Every build starts from a fresh copy of the keys, made outside the timer, because `filter8::build(std::vector&&)` and the reference's retry path both reorder their input. The fusefilter number includes sorting and deduplicating the keys, which `build` does and the reference does not (it requires distinct keys); the Bloom rows insert the keys one by one into a filter sized up front.
- Lookup: ns per call over all N present keys and over all N absent keys, best of 3 passes. The queries are independent, so the number is throughput of a pipelined loop, not the latency of one dependent call. A compiler barrier before each pass makes the loop run on every pass.
- Bits per key: the filter's bytes times 8 over N (`size_in_bytes()`, `binary_fuse8_size_in_bytes()`, `capacity() / 8` for Boost).
- False positives: the share of the N absent keys reported present.
- Checks: every filter must report all N present keys present, and the bench aborts with a message when one does not (a false negative). The filters hash differently, so their answers on absent keys are not equal; the false-positive rate is what is compared. fusefilter and the reference both measure 0.39% (they are the same construction; 1/256 is 0.39%), Boost's k=8 filter is sized to it, and k=6 at 9 bits per key is 1.28% to 1.32%.
- Runs: the whole bench ran 5 times with default flags and 3 times with `-march=native` one after another on each machine, with other timing jobs kept off (the load average at the start was 8 on the M4 Pro and 4 on the i9). The tables give the median of the runs and, in brackets, the smallest and largest. The spread is the A/A figure: fusefilter and the reference have the same lookup loop over the same layout and differ by less than that spread at both sizes on both machines.

## Machines and commands

- Apple M4 Pro, macOS 26, Apple clang 17.0.0 (clang-1700.3.19.1), `-O3 -DNDEBUG` (CMake Release).
- Intel Core i9-9980HK (8 cores, 2.4 GHz base), macOS 15, Apple clang 17.0.0 (clang-1700.6.4.2), `-O3 -DNDEBUG`.
- "Default flags" means CMake `Release` alone. The other flag set adds `-DCMAKE_CXX_FLAGS=-march=native`. It does not change any row beyond the spread on either machine, so the README uses the default flags a user gets.

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release -DFUSEFILTER_BENCH_COMPARATORS=ON
cmake --build build --target fusefilter_bench
build/fusefilter_bench 1000000 10000000
```

`FUSEFILTER_BENCH_COMPARATORS` needs CMake 3.18, Boost 1.89 or later with its CMake package (Homebrew `boost`), and network access for the fetch of the reference header. It is off by default and the `fusefilter` target never depends on it; without it the bench times `filter8` alone.

## Rows

Build in ns per key (lower is faster), lookup in ns per call (lower is faster).

### Apple M4 Pro, default flags (5 runs)

| Keys | Filter | Build (ns/key) | Present (ns) | Absent (ns) | Bits per key | False positives |
|---:|---|---:|---:|---:|---:|---:|
| 1,000,000 | fusefilter filter8 | 38.6 (38.4 to 39.1) | 1.6 (1.5 to 1.6) | 1.6 (1.6 to 1.7) | 9.04 | 0.39% |
| 1,000,000 | binary_fuse8_t (reference) | 20.1 (19.3 to 20.4) | 1.6 (1.5 to 1.6) | 1.6 (1.5 to 1.6) | 9.04 | 0.39% |
| 1,000,000 | boost::bloom k=6, same bits | 5.0 (4.8 to 5.1) | 3.2 (3.1 to 3.2) | 9.1 (8.9 to 9.2) | 9.04 | 1.28% |
| 1,000,000 | boost::bloom k=8, same rate | 6.3 (6.2 to 6.5) | 4.3 (4.1 to 4.3) | 9.8 (9.7 to 9.9) | 11.54 | 0.39% |
| 10,000,000 | fusefilter filter8 | 43.1 (42.7 to 43.3) | 2.0 (2.0 to 2.1) | 2.0 (2.0 to 2.1) | 9.02 | 0.39% |
| 10,000,000 | binary_fuse8_t (reference) | 26.0 (25.6 to 26.1) | 2.0 (2.0 to 2.1) | 2.0 (2.0 to 2.1) | 9.02 | 0.39% |
| 10,000,000 | boost::bloom k=6, same bits | 6.0 (6.0 to 6.0) | 4.2 (4.1 to 4.2) | 10.4 (10.4 to 11.1) | 9.02 | 1.32% |
| 10,000,000 | boost::bloom k=8, same rate | 7.4 (7.2 to 7.7) | 5.6 (5.5 to 5.8) | 12.1 (11.7 to 12.3) | 11.54 | 0.39% |

### Apple M4 Pro, `-march=native` (3 runs)

| Keys | Filter | Build (ns/key) | Present (ns) | Absent (ns) | Bits per key | False positives |
|---:|---|---:|---:|---:|---:|---:|
| 1,000,000 | fusefilter filter8 | 38.4 (38.3 to 39.0) | 1.6 (1.5 to 1.6) | 1.5 (1.5 to 1.6) | 9.04 | 0.39% |
| 1,000,000 | binary_fuse8_t (reference) | 19.8 (19.1 to 20.2) | 1.6 (1.5 to 1.6) | 1.5 (1.5 to 1.6) | 9.04 | 0.39% |
| 1,000,000 | boost::bloom k=6, same bits | 5.0 (4.9 to 5.0) | 3.1 (3.0 to 3.2) | 9.2 (9.0 to 9.2) | 9.04 | 1.28% |
| 1,000,000 | boost::bloom k=8, same rate | 6.4 (6.2 to 6.5) | 4.3 (4.2 to 4.4) | 9.9 (9.7 to 10.0) | 11.54 | 0.39% |
| 10,000,000 | fusefilter filter8 | 43.3 (43.2 to 43.5) | 2.0 (2.0 to 2.2) | 2.0 (2.0 to 2.1) | 9.02 | 0.39% |
| 10,000,000 | binary_fuse8_t (reference) | 26.0 (25.7 to 26.1) | 2.1 (2.0 to 2.1) | 2.1 (2.0 to 2.1) | 9.02 | 0.39% |
| 10,000,000 | boost::bloom k=6, same bits | 6.0 (5.9 to 6.0) | 4.2 (4.1 to 4.3) | 10.9 (10.5 to 11.0) | 9.02 | 1.32% |
| 10,000,000 | boost::bloom k=8, same rate | 7.3 (7.3 to 7.9) | 5.6 (5.5 to 5.7) | 12.5 (12.2 to 12.5) | 11.54 | 0.39% |

### Intel Core i9-9980HK, default flags (5 runs)

| Keys | Filter | Build (ns/key) | Present (ns) | Absent (ns) | Bits per key | False positives |
|---:|---|---:|---:|---:|---:|---:|
| 1,000,000 | fusefilter filter8 | 104.4 (101.3 to 107.9) | 5.6 (5.5 to 5.9) | 5.7 (5.0 to 6.1) | 9.04 | 0.39% |
| 1,000,000 | binary_fuse8_t (reference) | 38.2 (36.6 to 40.9) | 5.8 (5.5 to 6.1) | 5.6 (5.6 to 6.7) | 9.04 | 0.39% |
| 1,000,000 | boost::bloom k=6, same bits | 12.4 (11.6 to 12.6) | 10.7 (9.8 to 11.7) | 13.7 (13.5 to 16.2) | 9.04 | 1.28% |
| 1,000,000 | boost::bloom k=8, same rate | 15.3 (14.6 to 18.1) | 13.6 (13.3 to 16.3) | 15.7 (14.8 to 16.0) | 11.54 | 0.39% |
| 10,000,000 | fusefilter filter8 | 125.6 (125.1 to 136.1) | 13.7 (11.6 to 17.1) | 13.5 (11.8 to 16.6) | 9.02 | 0.39% |
| 10,000,000 | binary_fuse8_t (reference) | 53.2 (49.1 to 58.0) | 12.8 (11.6 to 14.6) | 12.9 (12.3 to 14.0) | 9.02 | 0.39% |
| 10,000,000 | boost::bloom k=6, same bits | 25.6 (24.4 to 42.2) | 21.4 (20.9 to 30.3) | 18.3 (17.1 to 20.2) | 9.02 | 1.32% |
| 10,000,000 | boost::bloom k=8, same rate | 47.4 (46.0 to 62.0) | 39.2 (37.0 to 41.0) | 24.8 (24.4 to 25.3) | 11.54 | 0.39% |

### Intel Core i9-9980HK, `-march=native` (3 runs)

| Keys | Filter | Build (ns/key) | Present (ns) | Absent (ns) | Bits per key | False positives |
|---:|---|---:|---:|---:|---:|---:|
| 1,000,000 | fusefilter filter8 | 103.1 (101.2 to 107.1) | 5.8 (5.4 to 6.1) | 5.5 (4.7 to 5.7) | 9.04 | 0.39% |
| 1,000,000 | binary_fuse8_t (reference) | 43.3 (37.9 to 46.8) | 5.6 (5.3 to 6.5) | 5.7 (5.3 to 5.8) | 9.04 | 0.39% |
| 1,000,000 | boost::bloom k=6, same bits | 13.7 (12.4 to 13.7) | 11.9 (11.4 to 12.2) | 16.3 (14.4 to 16.8) | 9.04 | 1.28% |
| 1,000,000 | boost::bloom k=8, same rate | 17.0 (14.7 to 18.2) | 15.7 (12.8 to 17.5) | 17.2 (14.8 to 17.5) | 11.54 | 0.39% |
| 10,000,000 | fusefilter filter8 | 124.7 (124.0 to 124.8) | 10.4 (9.6 to 16.1) | 10.7 (10.5 to 13.8) | 9.02 | 0.39% |
| 10,000,000 | binary_fuse8_t (reference) | 52.0 (51.8 to 52.5) | 11.7 (9.4 to 11.9) | 10.7 (9.2 to 11.4) | 9.02 | 0.39% |
| 10,000,000 | boost::bloom k=6, same bits | 25.5 (24.4 to 36.8) | 21.2 (20.3 to 35.8) | 19.7 (17.8 to 22.6) | 9.02 | 1.32% |
| 10,000,000 | boost::bloom k=8, same rate | 48.5 (46.8 to 77.0) | 41.0 (38.9 to 47.5) | 25.1 (24.2 to 25.2) | 11.54 | 0.39% |

## Reading the rows

- Lookup: fusefilter and the reference are the same speed within the spread, since they read the same three slots. Boost's k=8 filter, which reaches the same false-positive rate, is 2.4 to 2.9 times slower on a present key (1.8 to 6.2 times on an absent key).
- Space: fusefilter and the reference take 9.02 to 9.04 bits per key; the Bloom filter takes 11.54 bits per key for the same 0.39%, and 9.04 bits per key for 1.28% to 1.32%.
- Build: the reference builds 1.7 to 2.7 times faster than fusefilter, and a Bloom filter 2.7 to 8.4 times faster. A Bloom insert is one pass over the keys with no sort and no peeling; the build is paid once per immutable set.
