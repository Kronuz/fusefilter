// libFuzzer target: arbitrary bytes either fail to deserialize or give a filter whose
// lookups stay in bounds; the in-place view accepts exactly the same bytes and answers alike.
#include "fusefilter.hh"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
	const std::string_view bytes(reinterpret_cast<const char*>(data), size);
	auto f = fusefilter::filter8::deserialize(bytes);
	auto v = fusefilter::filter8_view::parse(bytes);
	if (f.has_value() != v.has_value()) { __builtin_trap(); }
	if (f) {
		for (std::uint64_t k = 0; k < 64; ++k) {
			if (f->contains(k * 0x9e3779b97f4a7c15ull) != v->contains(k * 0x9e3779b97f4a7c15ull)) { __builtin_trap(); }
		}
	}
	return 0;
}
