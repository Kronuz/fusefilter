# fusefilter

Part of [Kronuz C++ Libraries](https://github.com/Kronuz/cpp-libraries), a collection of small, reusable C++ libraries.

[![CI](https://github.com/Kronuz/fusefilter/actions/workflows/ci.yml/badge.svg)](https://github.com/Kronuz/fusefilter/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)
![Header-only](https://img.shields.io/badge/header--only-yes-green.svg)

Header-only binary fuse filters for C++20: an immutable 64-bit key set in about 9 bits per key, three reads per lookup.
It answers "maybe present" with no false negatives and about one false positive
in 256. Each key's hash picks one 8-bit slot in each of three consecutive
segments, and the slots' XOR is its fingerprint (Graf and Lemire, 2022). Reach
for it to skip disk reads for absent keys in a set that does not change.

![fusefilter: the key's hash picks three slots in consecutive segments; their XOR matches the fingerprint or the key is absent](docs/fusefilter.svg)

## ✨ Highlights

- **No false negatives.** A key the filter was built from always answers
  present.
- **1 in 256 false positives** in 9.0 to 9.5 bits per key, against a floor of 8
  for 8-bit fingerprints.
- **Three memory reads per lookup**, one per segment.
- **Lean builds.** `build(std::move(keys))` sorts and deduplicates in place,
  with about 24 bytes per key of working set; duplicates are ignored.
- **Serializable.** `serialize()` writes a versioned byte string, and
  `deserialize()` returns `std::nullopt` for malformed input.
- **Readable in place.** `filter8_view::parse()` answers from the serialized
  bytes, so a filter in a mapped file costs no memory of its own.

## 🚀 Quick start

```cpp
#include "fusefilter.hh"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

int main() {
	std::vector<std::uint64_t> keys;  // 64-bit hashes of your keys
	for (std::uint64_t i = 1; i <= 100000; ++i)
		keys.push_back(i * 0x9e3779b97f4a7c15);
	auto f = fusefilter::filter8::build(keys);  // duplicates are ignored

	std::printf("%d\n", f.contains(keys[42]));  // 1, never a false negative
	std::printf("%d\n", f.contains(12345));  // 0 here; 1 for about 1 in 256 absent keys
	std::printf("%zu bytes\n", f.size_in_bytes());  // 118784 bytes, 9.5 bits per key

	const std::string bytes = f.serialize();
	auto copy = fusefilter::filter8::deserialize(bytes);  // owns its array; nullopt when malformed
	auto view = fusefilter::filter8_view::parse(bytes);  // reads `bytes` in place, which must outlive it
	std::printf("%d %d\n", copy->contains(keys[7]), view->contains(keys[7]));  // 1 1
}
```

## 📦 Installation

Requires a C++20 compiler with `unsigned __int128` (GCC or Clang) and CMake 3.14
or newer; no dependencies. CI builds it on Linux (x86_64, arm64), macOS (arm64,
x86_64), and FreeBSD, plus ASan, UBSan, and TSan builds.

```cmake
include(FetchContent)
FetchContent_Declare(fusefilter
  GIT_REPOSITORY https://github.com/Kronuz/fusefilter.git
  GIT_TAG        a42bd15a136e7a5a21456fff083eedd40f3d02c1)  # or main for the tip
FetchContent_MakeAvailable(fusefilter)

target_link_libraries(my_app PRIVATE fusefilter::fusefilter)
```

```cpp
#include "fusefilter.hh"
```

## 🎯 When to use it

Use it in front of a disk read or a remote lookup for a key set built once:
SSTables, segment term lookups, immutable indexes.

Not for:

- **Sets that change.** Every change is a rebuild;
  [bloom-filter](https://github.com/Kronuz/bloom-filter) takes inserts.
- **Keys that are not 64-bit integers.** Hash strings and records first, for
  example with [hashes](https://github.com/Kronuz/hashes).
- **Rates below 1 in 256.** The fingerprints are 8 bits, and `filter8` is the
  only width.

## ⚡ Performance

`bench/bench.cc` builds a filter from N random 64-bit keys and looks up every
present key and N absent ones. Intel Core i9-9980HK, Apple clang, `-O3
-march=native`, with other work on the host:

| Keys | Build | Lookup (present / absent) | Bits per key | False positives |
|---:|---:|---:|---:|---:|
| 100,000 | 115 ns/key | 4.1 / 4.0 ns | 9.50 | 0.40% |
| 1,000,000 | 127 ns/key | 5.6 / 5.6 ns | 9.04 | 0.39% |
| 10,000,000 | 155 ns/key | 14.6 / 14.9 ns | 9.02 | 0.39% |

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS=-march=native
cmake --build build --target fusefilter_bench
build/fusefilter_bench 10000000
```

At 10 million keys the 11 MB filter is past the last-level cache, and each read
is a memory access.

## 📚 Documentation

- [ARCHITECTURE.md](ARCHITECTURE.md): lookup, construction, and the serialized
  form.
- [CHANGELOG.md](CHANGELOG.md): what changed.
- [AGENTS.md](AGENTS.md): repository map and invariants.
- [Catalog entry](https://github.com/Kronuz/cpp-libraries/blob/main/AGENTS.md#fusefilter)
  in Kronuz C++ Libraries.

## 🛠️ Building and testing

```sh
cmake -B build
cmake --build build
ctest --test-dir build
```

The test checks for false negatives from 0 to 1,000,000 keys, the false-positive
rate and size, duplicates, the serialization round trip, and that every
truncation and inconsistent layout is rejected. `-DFUSEFILTER_FUZZ=ON` with
clang builds the libFuzzer target `fusefilter_fuzz`.

## 🤝 Contributing

Issues and pull requests are welcome. Read [AGENTS.md](AGENTS.md) and the
catalog's [rules of engagement](https://github.com/Kronuz/cpp-libraries/blob/main/AGENTS.md#rules-of-engagement)
first; CI must pass on every platform.

## 📄 License

MIT, Copyright (c) 2026 Germán Méndez Bravo. See [LICENSE](LICENSE).
