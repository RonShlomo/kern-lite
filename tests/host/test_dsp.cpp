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

    dsp::MovingAverage<float, 3> filter;

    // Feed [0, 0, 0, 10, 10, 10]; expected [0, 0, 0, 3.33, 6.67, 10.0]
    assert(compare_floats(filter.update(0.0f), 0.0f));
    assert(compare_floats(filter.update(0.0f), 0.0f));
    assert(compare_floats(filter.update(0.0f), 0.0f));
    assert(compare_floats(filter.update(10.0f), 3.33f)); // (0 + 0 + 10) / 3
    assert(compare_floats(filter.update(10.0f), 6.67f)); // (0 + 10 + 10) / 3
    assert(compare_floats(filter.update(10.0f), 10.0f)); // (10 + 10 + 10) / 3
}

// TEST 2: THRESHOLD RISING & HYSTERESIS
void testThresholdRising() {
    std::cout << "Running testThresholdRising" << std::endl;

    dsp::ThresholdConfig cfg{10.0f, 90.0f, 5.0f};
    dsp::ThresholdDetector detector(cfg);

    assert(detector.update(50.0f) == dsp::ThresholdDetector::State::Normal);
    assert(detector.update(95.0f) == dsp::ThresholdDetector::State::HighAlert);
    // inside hysteresis zone (90 - 5 = 85): alert must persist
    assert(detector.update(88.0f) == dsp::ThresholdDetector::State::HighAlert);
    // below hi - hysteresis: clears
    assert(detector.update(84.9f) == dsp::ThresholdDetector::State::Normal);
}

// TEST 3: THRESHOLD FALLING
void testThresholdFalling() {
    std::cout << "Running testThresholdFalling" << std::endl;

    dsp::ThresholdConfig cfg{10.0f, 90.0f, 5.0f};
    dsp::ThresholdDetector detector(cfg);

    assert(detector.update(5.0f) == dsp::ThresholdDetector::State::LowAlert);
    // inside hysteresis zone (10 + 5 = 15): alert must persist
    assert(detector.update(12.0f) == dsp::ThresholdDetector::State::LowAlert);
    assert(detector.update(15.1f) == dsp::ThresholdDetector::State::Normal);
}

// TEST 4: NO CHATTER (STABILITY CHECK)
void testNoChatter() {
    std::cout << "Running testNoChatter" << std::endl;

    dsp::ThresholdConfig cfg{10.0f, 90.0f, 5.0f};
    dsp::ThresholdDetector detector(cfg);

    detector.update(100.0f); // force HighAlert
    // hover exactly at the boundary (hi - hysteresis): must not toggle
    for (int i = 0; i < 10; ++i) {
        assert(detector.update(85.0f) == dsp::ThresholdDetector::State::HighAlert);
    }
}

// TEST 5: MEMORY LAYOUT ASSERTIONS (compile-time)
void testMemoryLayout() {
    std::cout << "Running testMemoryLayout" << std::endl;

    static_assert(sizeof(storage::SensorRecord) == 32, "SensorRecord size must be exactly 32 bytes");
    static_assert(offsetof(storage::SensorRecord, crc32) == 28, "crc32 field must be at byte offset 28");
}

// TEST 6: RECORD CRC CROSS-IMPLEMENTATION VECTORS
//
// Expected values were computed independently by the Python ground station:
//   body = struct.pack('<IHHhhHHHBBB7x', <fields...>)
//   crc32(body)  ->  hardcoded below
// If these asserts fail, the C++ CRC parameters or the SensorRecord byte
// layout diverge from the Python implementation.
void testRecordCrc() {
    std::cout << "Running testRecordCrc" << std::endl;

    // Vector 1: all-zero record with seq = 1
    {
        storage::SensorRecord rec;
        std::memset(&rec, 0, sizeof(rec));
        rec.seq = 1;

        uint32_t crc = protocol::crc32(reinterpret_cast<const uint8_t*>(&rec), 28);
        assert(crc == 0x97581329u); // from Python: crc32(pack('<IHHhhHHHBBB7x', 0,0,1,0,0,0,0,0,0,0,0))
        rec.crc32 = crc;
    }

    // Vector 2: fully populated record, exercises signed fields and all offsets
    {
        storage::SensorRecord rec;
        std::memset(&rec, 0, sizeof(rec));
        rec.timestamp  = 12;
        rec.ms         = 345;
        rec.seq        = 5;
        rec.lm35_c     = 253;     // 25.3 degC
        rec.dht_temp_c = -15;     // -1.5 degC (signed)
        rec.dht_hum    = 605;     // 60.5 %RH
        rec.light      = 32768;
        rec.pot        = 65535;
        rec.alert_bits = 0x01;
        rec.state      = 1;
        rec.fault_bits = 0x02;

        uint32_t crc = protocol::crc32(reinterpret_cast<const uint8_t*>(&rec), 28);
        assert(crc == 0x8CA5C1DBu); // from Python, same fields
    }
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