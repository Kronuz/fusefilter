# fusefilter architecture

How `fusefilter.hh` builds and reads a binary fuse filter (Graf and Lemire,
"Binary Fuse Filters: Fast and Smaller Than Xor Filters", 2022). For usage see
[README.md](README.md); for the repository map and invariants see
[AGENTS.md](AGENTS.md).

## Lookup

Each key is mixed with the filter's seed (murmur3's 64-bit finalizer). The hash
picks three slots, one in each of three consecutive segments of a power-of-two
length, and an 8-bit fingerprint. The filter stores fingerprints so that the XOR
of the three slots equals the key's fingerprint: a match means "probably
present", anything else "certainly absent". A lookup is three memory reads.

## Construction

Construction peels the 3-hypergraph: a slot that only one key maps to fixes that
key's fingerprint last. When a core remains, the build retries with a new seed;
after 100 seeds the table grows a segment, so a set that builds at the paper's
size keeps its bytes. Hashes are processed in slot order, so the counting pass
walks memory nearly sequentially. Sizing follows the paper: 1.125 slots per key
for large sets, more for small ones.

`build(std::vector<std::uint64_t>&&)` sorts and deduplicates the keys in place,
with a working set of about 24 bytes per key past the keys; the `std::span`
overload copies them first, about 46 bytes per key.

## Serialized form

```
[version u8][seed u64][segment length u32][segment count length u32][array length u32][fingerprints]
```

All fields are little-endian. `filter8::deserialize` and `filter8_view::parse`
share one validation, `detail::layout::parse`, which accepts only layouts whose
three positions stay inside the array.
