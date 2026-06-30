#pragma once

#include "frame.hpp"

namespace kern::protocol {
	enum class DecodeResult {
		NeedMore,
		FrameReady,
		CrcError,
		SyncError
	};

	size_t encode(const Frame& f, uint8_t* outBuf, size_t outSize);

	class Decoder {
	public:
		DecodeResult feed(uint8_t byte);
		const Frame& frame() const { return m_frame; }
		void reset();

	private:
		Frame m_frame{};
	};
}
