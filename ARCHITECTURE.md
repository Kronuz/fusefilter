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

A build hashes every key with a seed and groups the hashes by their top bits, about
4,096 to a group. A hash's first slot grows with the hash, so a group's slots lie near
each other and the counting pass that follows walks the tables nearly in order. That
pass adds each hash to its three slots, which hold a count of keys (a byte) and the XOR
of their hashes. A slot with 256 keys wraps its count; the counts add up to 3n unless
one did, so the build checks the sum and treats a wrap as a failed attempt.

Peeling takes slots from a first-in first-out queue that starts with the slots that
hold one key, in slot order. Taking a slot reads its key's hash from the XOR, removes
the key from its other two slots, and queues those that are left with one key; the
key's own slot is not touched again. The hashes go to a stack in removal order. The
queue is known ahead, so the build fetches the lines of the slots 16 entries on, and of
the slots that the key 8 entries on will touch, while it works on the current one;
those reads are what the peel waits for. When every key was removed, fingerprints are
assigned in reverse removal order, each key to the slot it was removed through. When
keys remain (a core), the build retries with the next seed; after 100 seeds the table
grows a segment, so a set that builds at the paper's size keeps its bytes. Sizing
follows the paper: 1.125 slots per key for large sets, more for small ones.

Duplicates are two edges on the same three slots, and no peel removes them. The build
does not look for them before it peels. When the first attempt fails, it reads the keys
whose three slots all hold two or more keys (the core, which holds every duplicate),
sorts their hashes, and looks for two that are equal; after a wrapped count it sorts
all of them. If it finds a pair, it sorts and deduplicates the keys and starts the
seeds over; otherwise the failure was bad luck and the next seed follows. The 2-core
does not depend on the order of the peel, so the seed that succeeds is the one a build
that sorted first would find, and a set builds the same bytes whatever its order and its
duplicates. A set without duplicates is never sorted.

`build(std::span)` reads the keys and copies them only to deduplicate;
`build(std::vector&&)` deduplicates in place and leaves the keys untouched when there
is nothing to remove. The working set past the keys is about 24 bytes per key at 1.125
slots per key: 8 for the hashes (then the removal stack), 1 for each key's slot choice,
9 for the slot tables, and 4.5 for the queue.

## Serialized form

```
[version u8][seed u64][segment length u32][segment count length u32][array length u32][fingerprints]
```

All fields are little-endian. `filter8::deserialize` and `filter8_view::parse`
share one validation, `detail::layout::parse`, which accepts only layouts whose
three positions stay inside the array.
