#include "codec.hpp"

namespace kern::protocol {
	size_t encode(const Frame&, uint8_t*, size_t)
	{
		return 0;
	}

	DecodeResult Decoder::feed(uint8_t) {

		return DecodeResult::NeedMore;
	}

	void Decoder::reset()
	{
		m_frame = {};
	}
}
