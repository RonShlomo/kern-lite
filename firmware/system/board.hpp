#pragma once

#include "stm32l4xx_hal.h"
#include <cstdint>

namespace kern::board {
	struct Pin {
		GPIO_TypeDef* port;
		uint8_t n;
	};

	inline constexpr Pin LED1_BLUE {GPIOC, 9};
	inline constexpr Pin LED2_RED {GPIOB, 8};
	inline constexpr Pin RGB_R {GPIOC, 7};
	inline constexpr Pin RGB_G {GPIOC, 6};
	inline constexpr Pin RGB_B {GPIOC, 8};
	inline constexpr Pin SD_CS {GPIOB, 6};
	inline constexpr Pin DHT_DATA {GPIOB, 5};
	inline constexpr Pin SW1 {GPIOA, 10};
	inline constexpr Pin SW2 {GPIOB, 3};
	inline constexpr Pin B1 {GPIOC, 13};

	enum class AdcChannel : uint32_t {
		Pot = ADC_CHANNEL_5,
		Photodiode = ADC_CHANNEL_6,
		Lm35 = ADC_CHANNEL_9,
	};
}
