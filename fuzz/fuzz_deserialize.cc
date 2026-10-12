// libFuzzer target: arbitrary bytes either fail to deserialize or give a filter whose
// lookups stay in bounds.
#include "fusefilter.hh"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
	auto f = fusefilter::filter8::deserialize(std::string_view(reinterpret_cast<const char*>(data), size));
	if (f) {
		for (std::uint64_t k = 0; k < 64; ++k) { (void)f->contains(k * 0x9e3779b97f4a7c15ull); }
	}
	return 0;
}
