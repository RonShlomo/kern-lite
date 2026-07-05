#pragma once

#include <cstdint>
#include <cstddef>

namespace kern::protocol {
	constexpr uint8_t kStx = 0xAB;
	constexpr uint8_t kEtx = 0xCD;
	constexpr size_t kMaxPayload = 256;
	constexpr size_t kFrameOverhead = 9;

	enum class FrameType : uint8_t {
		CmdStart = 0x01,
		CmdStop = 0x02,
		CmdStatus = 0x03,
		CmdReplay = 0x04,
		CmdErase = 0x06,
		Status = 0x12,
		Record = 0x10,
		Ack = 0x20,
		Nack = 0x21,
	};

	enum class NackCode : uint8_t {
	    CrcError     = 0x01,
	    BadCommand   = 0x02,
	    InvalidState = 0x03,
	    StorageError = 0x04,
	    BadMagic     = 0x06,
	};

	struct Frame {
		FrameType type{};
		uint8_t payload[kMaxPayload]{};
		uint16_t len = 0;
	};
}
