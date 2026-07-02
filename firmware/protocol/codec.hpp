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

		enum class State {
			WaitStx,
			Type,
			LenLo,
			LenHi,
			Payload,
			Crc0,
			Crc1,
			Crc2,
			Crc3,
			WaitEtx
		};

		State m_state = State::WaitStx;

		uint16_t m_payloadIndex = 0;
		uint32_t m_receivedCrc = 0;
		uint32_t m_accumulatedCrc = 0;
	};
}
