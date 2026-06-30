#pragma once

#include "stm32l4xx_hal.h"

namespace kern::hal::watchdog {
	inline void kick(IWDG_HandleTypeDef& h) {
		HAL_IWDG_Refresh(&h);
	}
}
