#include <iostream>
#include <cassert>
#include <cmath>     // std::abs()
#include <cstring>   // std::memset()
#include <cstddef>   // offsetof()

#include "../../firmware/dsp/moving_average.hpp"
#include "../../firmware/dsp/threshold_detector.hpp"
#include "../../firmware/storage/sensor_record.hpp"
#include "../../firmware/protocol/crc32.hpp"

using namespace kern;

// float comparison
bool compare_floats(float a, float b, float epsilon = 0.01f)
{
    return std::abs(a - b) < epsilon;
}

// TEST 1: MOVING AVERAGE
void testMovingAverage() {
    std::cout << "Running testMovingAverage" << std::endl;

    // Create a Moving Average filter with a window size of 3
    dsp::MovingAverage<float, 3> filter;

    // Feed the sequence: [0, 0, 0, 10, 10, 10]
    // The expected outputs are: 0, 0, 0, 3.33, 6.67, 10.0

    assert(compare_floats(filter.update(0.0f), 0.0f));
    assert(compare_floats(filter.update(0.0f), 0.0f));
    assert(compare_floats(filter.update(0.0f), 0.0f));

    // Now we introduce the 10s. The window shifts.
    assert(compare_floats(filter.update(10.0f), 3.33f)); // (0 + 0 + 10) / 3
    assert(compare_floats(filter.update(10.0f), 6.67f)); // (0 + 10 + 10) / 3
    assert(compare_floats(filter.update(10.0f), 10.0f)); // (10 + 10 + 10) / 3
}

// TEST 2: THRESHOLD RISING & HYSTERESIS
void testThresholdRising() {
    std::cout << "Running testThresholdRising" << std::endl;

    // Configure threshold: Normal range is 10 to 90. Hysteresis is 5.
    dsp::ThresholdConfig cfg{10.0f, 90.0f, 5.0f};
    dsp::ThresholdDetector detector(cfg);

    // Start normal
    assert(detector.update(50.0f) == dsp::ThresholdDetector::State::Normal);

    // Cross the 'hi' boundary -> Should trigger HighAlert
    assert(detector.update(95.0f) == dsp::ThresholdDetector::State::HighAlert);

    // Drop back down, BUT not enough to clear the hysteresis zone (90 - 5 = 85)
    // 88 is inside the hysteresis zone, so the alert should persist!
    assert(detector.update(88.0f) == dsp::ThresholdDetector::State::HighAlert);

    // Drop below (hi - hysteresis - epsilon) -> Should clear the alert
    assert(detector.update(84.9f) == dsp::ThresholdDetector::State::Normal);
}

// TEST 3: THRESHOLD FALLING
void testThresholdFalling() {
    std::cout << "Running testThresholdFalling" << std::endl;

    dsp::ThresholdConfig cfg{10.0f, 90.0f, 5.0f};
    dsp::ThresholdDetector detector(cfg);

    // Cross the 'lo' boundary -> Should trigger LowAlert
    assert(detector.update(5.0f) == dsp::ThresholdDetector::State::LowAlert);

    // Rise back up, but still inside hysteresis zone (10 + 5 = 15)
    assert(detector.update(12.0f) == dsp::ThresholdDetector::State::LowAlert);

    // Rise above (lo + hysteresis + epsilon) -> Should clear the alert
    assert(detector.update(15.1f) == dsp::ThresholdDetector::State::Normal);
}

// TEST 4: NO CHATTER (STABILITY CHECK)
void testNoChatter() {
    std::cout << "Running testNoChatter" << std::endl;

    dsp::ThresholdConfig cfg{10.0f, 90.0f, 5.0f};
    dsp::ThresholdDetector detector(cfg);

    // Force a HighAlert
    detector.update(100.0f);

    // Hover exactly at the boundary (hi - hysteresis)
    // The state must NOT toggle. It should remain HighAlert.
    for (int i = 0; i < 10; ++i) {
        assert(detector.update(85.0f) == dsp::ThresholdDetector::State::HighAlert);
    }
}

// TEST 5: MEMORY LAYOUT ASSERTIONS
void testMemoryLayout() {
    std::cout << "Running testMemoryLayout" << std::endl;

    // These checks run at compile-time. If the struct is packed incorrectly,
    // the code will refuse to compile, preventing bugs on the SD card later.
    static_assert(sizeof(storage::SensorRecord) == 32, "SensorRecord size must be exactly 32 bytes");
    static_assert(offsetof(storage::SensorRecord, crc32) == 28, "crc32 field must be at byte offset 28");
}

// TEST 6: RECORD CRC VALIDATION
void testRecordCrc() {
    std::cout << "Running testRecordCrc" << std::endl;

    storage::SensorRecord rec;

    // Zero out the entire memory block to ensure no garbage bytes affect the CRC
    std::memset(&rec, 0, sizeof(rec));

    // Set known fields
    rec.seq = 1;

    // Compute CRC over the first 28 bytes (everything before the crc32 field itself)
    uint32_t expectedCrc = protocol::crc32(reinterpret_cast<const uint8_t*>(&rec), 28);

    // Write it back to the struct (as the firmware does)
    rec.crc32 = expectedCrc;

    // Verify it matches
    assert(rec.crc32 == expectedCrc);
}

// MAIN ENTRY POINT
int main() {
    std::cout << "Starting DSP and Storage Host Tests" << std::endl;

    testMovingAverage();
    testThresholdRising();
    testThresholdFalling();
    testNoChatter();
    testMemoryLayout();
    testRecordCrc();

    std::cout << "test_dsp: all tests passed" << std::endl;
    return 0;
}
