#pragma once

#include "../hal/adc.hpp"

namespace kern::sensors {

class Photodiode {
public:
    void init() {}

    float readNormalized()
    {
        uint16_t raw = hal::adc::read(board::AdcChannel::Photodiode);
        float volts = hal::adc::toVolts(raw);
        return volts / 3.3f;
    }
};

}
