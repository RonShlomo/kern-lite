#include "protocol/crc32.hpp"
#include "protocol/codec.hpp"
#include "protocol/frame.hpp"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>

using namespace kern::protocol;

namespace {

DecodeResult feedUntilDone(Decoder& decoder, const uint8_t* data, std::size_t len)
{
    DecodeResult result = DecodeResult::NeedMore;

    for (std::size_t i = 0; i < len; ++i) {
        result = decoder.feed(data[i]);

        if (result == DecodeResult::FrameReady ||
            result == DecodeResult::CrcError ||
            result == DecodeResult::SyncError) {
            return result;
        }
    }

    return result;
}

void assertSameFrame(const Frame& expected, const Frame& actual)
{
    assert(actual.type == expected.type);
    assert(actual.len == expected.len);

    if (expected.len > 0) {
        assert(std::memcmp(actual.payload, expected.payload, expected.len) == 0);
    }
}

void testCrcKat()
{
    const char* text = "123456789";
    const auto* data = reinterpret_cast<const uint8_t*>(text);
    const std::size_t len = std::strlen(text);

    const uint32_t oneShot = crc32(data, len);
    assert(oneShot == 0xCBF43926u);

    uint32_t incremental = crc32Begin();
    incremental = crc32Update(incremental, data, 4);
    incremental = crc32Update(incremental, data + 4, len - 4);
    incremental = crc32Finalize(incremental);

    assert(incremental == oneShot);
}

void testProtocolConstants()
{
    assert(kStx == 0xAB);
    assert(kEtx == 0xCD);
    assert(kMaxPayload == 256);

    // Important: frame overhead is STX + TYPE + LEN(2) + CRC(4) + ETX = 9
    assert(kFrameOverhead == 9);

    assert(static_cast<uint8_t>(FrameType::CmdStart) == 0x01);
    assert(static_cast<uint8_t>(FrameType::CmdStop) == 0x02);
    assert(static_cast<uint8_t>(FrameType::CmdStatus) == 0x03);
    assert(static_cast<uint8_t>(FrameType::CmdReplay) == 0x04);
    assert(static_cast<uint8_t>(FrameType::CmdErase) == 0x06);

    assert(static_cast<uint8_t>(FrameType::Record) == 0x10);
    assert(static_cast<uint8_t>(FrameType::Status) == 0x12);
    assert(static_cast<uint8_t>(FrameType::Ack) == 0x20);
    assert(static_cast<uint8_t>(FrameType::Nack) == 0x21);

    assert(static_cast<uint8_t>(NackCode::CrcError) == 0x01);
    assert(static_cast<uint8_t>(NackCode::BadCommand) == 0x02);
    assert(static_cast<uint8_t>(NackCode::InvalidState) == 0x03);
    assert(static_cast<uint8_t>(NackCode::StorageError) == 0x04);
    assert(static_cast<uint8_t>(NackCode::BadMagic) == 0x06);
}

void testAckRoundTrip()
{
    Frame original{};
    original.type = FrameType::Ack;
    original.len = 0;

    uint8_t encoded[265]{};
    const std::size_t written = encode(original, encoded, sizeof(encoded));

    assert(written == 9);
    assert(encoded[0] == kStx);
    assert(encoded[1] == static_cast<uint8_t>(FrameType::Ack));
    assert(encoded[2] == 0x00);
    assert(encoded[3] == 0x00);
    assert(encoded[8] == kEtx);

    Decoder decoder{};
    const DecodeResult result = feedUntilDone(decoder, encoded, written);

    assert(result == DecodeResult::FrameReady);
    assertSameFrame(original, decoder.frame());
}

void testStatusRoundTrip()
{
    Frame original{};
    original.type = FrameType::Status;
    original.len = 14;

    for (uint16_t i = 0; i < original.len; ++i) {
        original.payload[i] = static_cast<uint8_t>(i + 1);
    }

    uint8_t encoded[265]{};
    const std::size_t written = encode(original, encoded, sizeof(encoded));

    assert(written == kFrameOverhead + original.len);
    assert(encoded[0] == kStx);
    assert(encoded[1] == static_cast<uint8_t>(FrameType::Status));
    assert(encoded[2] == 14);
    assert(encoded[3] == 0);
    assert(encoded[written - 1] == kEtx);

    Decoder decoder{};
    const DecodeResult result = feedUntilDone(decoder, encoded, written);

    assert(result == DecodeResult::FrameReady);
    assertSameFrame(original, decoder.frame());
}

void testMaxPayloadRoundTrip()
{
    Frame original{};
    original.type = FrameType::Record;
    original.len = kMaxPayload;

    for (uint16_t i = 0; i < original.len; ++i) {
        original.payload[i] = static_cast<uint8_t>(i & 0xFFu);
    }

    uint8_t encoded[265]{};
    const std::size_t written = encode(original, encoded, sizeof(encoded));

    assert(written == 265);
    assert(encoded[0] == kStx);
    assert(encoded[1] == static_cast<uint8_t>(FrameType::Record));
    assert(encoded[2] == 0x00);
    assert(encoded[3] == 0x01);
    assert(encoded[written - 1] == kEtx);

    Decoder decoder{};
    const DecodeResult result = feedUntilDone(decoder, encoded, written);

    assert(result == DecodeResult::FrameReady);
    assertSameFrame(original, decoder.frame());
}

void testCorruptionReturnsCrcError()
{
    Frame original{};
    original.type = FrameType::Status;
    original.len = 14;

    for (uint16_t i = 0; i < original.len; ++i) {
        original.payload[i] = static_cast<uint8_t>(10 + i);
    }

    uint8_t encoded[265]{};
    const std::size_t written = encode(original, encoded, sizeof(encoded));

    assert(written == kFrameOverhead + original.len);

    // Layout:
    // 0 = STX
    // 1 = TYPE
    // 2 = LEN_LO
    // 3 = LEN_HI
    // 4... = PAYLOAD
    encoded[4 + 5] ^= 0x55u;

    Decoder decoder{};
    const DecodeResult result = feedUntilDone(decoder, encoded, written);

    assert(result == DecodeResult::CrcError);
}

void testResyncAfterGarbage()
{
    Frame original{};
    original.type = FrameType::Ack;
    original.len = 0;

    uint8_t encoded[265]{};
    const std::size_t written = encode(original, encoded, sizeof(encoded));

    Decoder decoder{};

    const uint8_t garbage[12] = {
        0x00, 0x11, 0x22, 0x33,
        0x44, 0x55, 0x66, 0x77,
        0x88, 0x99, 0xAA, 0xCC
    };

    // Garbage before STX should not prevent the next real frame from decoding.
    for (uint8_t b : garbage) {
        DecodeResult r = decoder.feed(b);
        assert(r == DecodeResult::NeedMore || r == DecodeResult::SyncError);
    }

    const DecodeResult result = feedUntilDone(decoder, encoded, written);

    assert(result == DecodeResult::FrameReady);
    assertSameFrame(original, decoder.frame());
}

void testOversizedLenReturnsSyncError()
{
    Decoder decoder{};

    assert(decoder.feed(kStx) == DecodeResult::NeedMore);
    assert(decoder.feed(static_cast<uint8_t>(FrameType::Record)) == DecodeResult::NeedMore);
    assert(decoder.feed(0xFF) == DecodeResult::NeedMore);

    // LEN = 0x01FF = 511, larger than 256
    assert(decoder.feed(0x01) == DecodeResult::SyncError);
}

void testEncodeRejectsSmallBuffer()
{
    Frame frame{};
    frame.type = FrameType::Ack;
    frame.len = 0;

    uint8_t tooSmall[8]{};
    const std::size_t written = encode(frame, tooSmall, sizeof(tooSmall));

    assert(written == 0);
}

void testEncodeRejectsTooLargePayload()
{
    Frame frame{};
    frame.type = FrameType::Record;
    frame.len = static_cast<uint16_t>(kMaxPayload + 1);

    uint8_t encoded[300]{};
    const std::size_t written = encode(frame, encoded, sizeof(encoded));

    assert(written == 0);
}

} // namespace

int main()
{
    testCrcKat();
    testProtocolConstants();
    testAckRoundTrip();
    testStatusRoundTrip();
    testMaxPayloadRoundTrip();
    testCorruptionReturnsCrcError();
    testResyncAfterGarbage();
    testOversizedLenReturnsSyncError();
    testEncodeRejectsSmallBuffer();
    testEncodeRejectsTooLargePayload();

    std::cout << "test_codec: all tests passed\n";
    return 0;
}
