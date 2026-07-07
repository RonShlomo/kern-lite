#include "codec.hpp"
#include "crc32.hpp"

namespace kern::protocol {
	size_t encode(const Frame& f, uint8_t* outBuf, size_t outSize)
	{
		// calc the min requires buffer size
		size_t requiredSize = kFrameOverhead + f.len;

		if (outSize < requiredSize || f.len > kMaxPayload) {
			return 0;
		}

		size_t i = 0;
		// start of text marker
		outBuf[i++] = kStx;
		size_t crcStart = i;

		// write frame type
		outBuf[i++] = static_cast<uint8_t>(f.type);

		// write length in little endian format using bitwise ops
		outBuf[i++] = static_cast<uint8_t>(f.len & 0xFF);
		outBuf[i++] = static_cast<uint8_t>((f.len >> 8) & 0xFF);

		// write payload bytes
		for (size_t j = 0; j < f.len; ++j) {
			outBuf[i++] = f.payload[j];
		}

		uint32_t crc = crc32(&outBuf[crcStart], 1 + 2 + f.len);

		// Write the 32bit CRC in little endian
		outBuf[i++] = static_cast<uint8_t>(crc & 0xFF);
		outBuf[i++] = static_cast<uint8_t>((crc >> 8) & 0xFF);
		outBuf[i++] = static_cast<uint8_t>((crc >> 16) & 0xFF);
		outBuf[i++] = static_cast<uint8_t>((crc >> 24) & 0xFF);

		// end of text marker
		outBuf[i++] = kEtx;

		// return the total number of bytes written to outBuf
		return i;
	}

	DecodeResult Decoder::feed(uint8_t byte)
	{
		// state machine to process incoming bytes one by one
		switch(m_state) {
			case State::WaitStx:
				if (byte == kStx) {
					// clear leftover data for safety before starting a new frame
					reset();
					m_state = State::Type;
					m_accumulatedCrc = crc32Begin();
					return DecodeResult::NeedMore;
				}
				// not an kStx, ignore
				return DecodeResult::NeedMore;

			case State::Type:
				// get the frame type opcode
				m_frame.type = static_cast<FrameType>(byte);
				m_state = State::LenLo;
				m_accumulatedCrc = crc32Update(m_accumulatedCrc, &byte, 1);
				return DecodeResult::NeedMore;

			case State::LenLo:
				// get the lower 8 bits of the payload length
				m_frame.len = byte;
				m_state = State::LenHi;
				m_accumulatedCrc = crc32Update(m_accumulatedCrc, &byte, 1);
				return DecodeResult::NeedMore;

			case State::LenHi:
				// get the upper 8 bits and reconstruct the 16 bit length (little endian)
				m_frame.len |= static_cast<uint16_t>(byte << 8);
				m_accumulatedCrc = crc32Update(m_accumulatedCrc, &byte, 1);

				// validate length to prevent buffer overflow
				if (m_frame.len > kMaxPayload) {
					reset();
					// invalid len, abort frame decoding
					return DecodeResult::SyncError;
				}

				m_payloadIndex = 0;
				// if length is 0, skip the payload state and go straight to CRC reading
				if (m_frame.len == 0) {
					m_state = State::Crc0;
				} else {
					m_state = State::Payload;
				}
				return DecodeResult::NeedMore;

			case State::Payload:
				// store incoming payload bytes sequentially
				m_frame.payload[m_payloadIndex++] = byte;
				m_accumulatedCrc = crc32Update(m_accumulatedCrc, &byte, 1);

				// check if we have received the full payload as defined by m_frame.len
				if (m_payloadIndex >= m_frame.len) {
					m_state = State::Crc0;
				}
				return DecodeResult::NeedMore;

			case State::Crc0:
				// reconstruct the 32 bit CRC received from the sender (little endian)
				m_receivedCrc = byte;
				m_state = State::Crc1;
				return DecodeResult::NeedMore;

			case State::Crc1:
				m_receivedCrc |= static_cast<uint32_t>(byte << 8);
				m_state = State::Crc2;
				return DecodeResult::NeedMore;

			case State::Crc2:
				m_receivedCrc |= static_cast<uint32_t>(byte << 16);
				m_state = State::Crc3;
				return DecodeResult::NeedMore;

			case State::Crc3:
				m_receivedCrc |= static_cast<uint32_t>(byte << 24);
				m_state = State::WaitEtx;
				return DecodeResult::NeedMore;

			case State::WaitEtx:
				// expecting the end of text mark to close the frame
				if (byte == kEtx) {
					m_state = State::WaitStx; // Ready for the next frame immediately

					m_accumulatedCrc = crc32Finalize(m_accumulatedCrc);

					// verify that the calculated CRC match the received one
					if (m_accumulatedCrc == m_receivedCrc) {
						return DecodeResult::FrameReady;
					} else {
						return DecodeResult::CrcError;
					}
				} else {
					// received unexpected byte instead of ETX
					m_state = State::WaitStx;
					return DecodeResult::SyncError;
				}
		}

		return DecodeResult::SyncError;
	}

	void Decoder::reset()
	{
		// reset all internal states and buffers to their default values
		m_frame = {};
		m_state = State::WaitStx;
		m_payloadIndex = 0;
		m_receivedCrc = 0;
		m_accumulatedCrc = 0;
	}
}
