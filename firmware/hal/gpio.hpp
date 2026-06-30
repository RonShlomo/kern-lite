#pragma once

#include "../system/board.hpp"

namespace kern::hal::gpio {
	inline uint16_t mask(board::Pin p)
	{
		return static_cast<uint16_t>(1u << p.n);
	}

	inline void set(board::Pin p)
	{
		HAL_GPIO_WritePin(p.port, mask(p), GPIO_PIN_SET);
	}

	inline void clear(board::Pin p)
	{
		HAL_GPIO_WritePin(p.port, mask(p), GPIO_PIN_RESET);
	}

	inline void toggle(board::Pin p)
	{
		HAL_GPIO_TogglePin(p.port, mask(p));
	}

	inline bool read(board::Pin p)
	{
		return HAL_GPIO_ReadPin(p.port, mask(p)) == GPIO_PIN_SET;
	}
}
