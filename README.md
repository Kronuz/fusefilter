# fusefilter

Binary fuse filters for C++20 (Graf and Lemire, "Binary Fuse Filters: Fast and Smaller
Than Xor Filters", 2022): an immutable set of 64-bit keys that answers "maybe present"
with no false negatives, a 1/256 false-positive rate, and three memory reads per lookup,
in about 9 bits per key. Header-only, no dependencies.

## Usage

```cpp
#include "fusefilter.hh"

std::vector<std::uint64_t> keys = /* hashes of your keys */;
auto f = fusefilter::filter8::build(keys);   // duplicates are ignored
bool maybe = f.contains(key_hash);           // false: certainly absent
std::string bytes = f.serialize();
auto g = fusefilter::filter8::deserialize(bytes);  // nullopt when malformed
```

## How it works

Each key hashes to three slots, one in each of three consecutive segments of a
power-of-two length, and the filter stores 8-bit fingerprints such that the three
slots' XOR equals the key's fingerprint. Construction peels the 3-hypergraph (a slot
that only one key maps to fixes that key's fingerprint last) and retries with a new
seed in the rare case a core remains; hashes are processed in slot order so the counting
pass walks memory nearly sequentially. Sizing follows the paper: 1.125 slots per key for
large sets, more for small ones.

## Performance

Random 64-bit keys on an Intel i9-9980HK, Apple clang `-O3 -march=native`
(`bench/bench.cc`):

| Keys | Build | Lookup (present / absent) | Bits per key | False positives |
|---:|---:|---:|---:|---:|
| 100,000 | 115 ns/key | 4.1 / 4.0 ns | 9.50 | 0.40% |
| 1,000,000 | 127 ns/key | 5.6 / 5.6 ns | 9.04 | 0.39% |
| 10,000,000 | 155 ns/key | 14.6 / 14.9 ns | 9.02 | 0.39% |

The host was not idle; lookups past the last-level cache (10M keys, 11 MB) pay a
memory access each.

## Correctness

The test checks no false negatives from 0 to 1,000,000 keys, the false-positive rate and
size, duplicates, the serialization round trip, and that every truncation and
inconsistent layout is rejected. `fuzz/fuzz_deserialize.cc` is a libFuzzer target (227M
runs clean under ASan and UBSan).

## Build and test

```sh
cmake -S . -B build && cmake --build build && ctest --test-dir build
build/fusefilter_bench 10000000
```

## License

MIT. See [LICENSE](LICENSE).
