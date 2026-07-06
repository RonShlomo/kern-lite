#pragma once

#include "../hal/gpio.hpp"
#include "../system/board.hpp"
#include "stm32l4xx_hal.h"
#include <cstdint>

namespace kern::sensors {

	enum class PressType {
		None,
		Short,
		Long
	};

	class Buttons {
	public:
		void init()
		{
			uint32_t now = HAL_GetTick();

			// Store the initial button state.
			m_lastRawPressed = isPressed();
			m_stablePressed = m_lastRawPressed;
			m_lastRawChangeMs = now;

			// If the button is already pressed at init, start timing it.
			m_pressStartMs = m_stablePressed ? now : 0;
		}

		PressType pollSw1()
		{
			uint32_t now = HAL_GetTick();
			bool rawPressed = isPressed();

			// Detect any raw change and restart the debounce timer.
			if (rawPressed != m_lastRawPressed) {
				m_lastRawPressed = rawPressed;
				m_lastRawChangeMs = now;
			}

			// Ignore changes until the signal has been stable long enough.
			if ((now - m_lastRawChangeMs) < kDebounceMs) {
				return PressType::None;
			}

			// Accept the new stable state after debounce.
			if (rawPressed != m_stablePressed) {
				m_stablePressed = rawPressed;

				// Button was pressed: remember when the press started.
				if (m_stablePressed) {
					m_pressStartMs = now;
					return PressType::None;
				}

				// Button was released: classify the press duration.
				uint32_t heldMs = now - m_pressStartMs;

				if (heldMs >= kLongPressMs) {
					return PressType::Long;
				}

				return PressType::Short;
			}

			return PressType::None;
		}

	private:
		static constexpr uint32_t kDebounceMs = 30;
		static constexpr uint32_t kLongPressMs = 1000;

		bool isPressed() const
		{
			// SW1 uses pull-up: released = HIGH, pressed = LOW.
			return !hal::gpio::read(board::SW1);
		}

		bool m_lastRawPressed = false;      // Last immediate GPIO reading.
		bool m_stablePressed = false;       // Debounced button state.
		uint32_t m_lastRawChangeMs = 0;     // Time of the last raw edge.
		uint32_t m_pressStartMs = 0;        // Time when the stable press began.
	};

}
