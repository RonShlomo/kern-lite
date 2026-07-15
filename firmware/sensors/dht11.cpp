#include "dht11.hpp"
#include "../system/board.hpp"
#include "../hal/gpio.hpp"
#include "stm32l4xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include <cstdint>

extern TIM_HandleTypeDef htim6;

namespace kern::sensors {

	namespace {

	uint16_t pinMask()
	{
		return kern::hal::gpio::mask(kern::board::DHT_DATA);
	}

	bool readPin()
	{
		return HAL_GPIO_ReadPin(kern::board::DHT_DATA.port, pinMask()) == GPIO_PIN_SET;
	}

	void setPinOutputOpenDrain()
	{
		GPIO_InitTypeDef gpio{};
		gpio.Pin = pinMask();
		gpio.Mode = GPIO_MODE_OUTPUT_OD;
		gpio.Pull = GPIO_PULLUP;
		gpio.Speed = GPIO_SPEED_FREQ_LOW;

		HAL_GPIO_Init(kern::board::DHT_DATA.port, &gpio);
	}

	void setPinInput()
	{
		GPIO_InitTypeDef gpio{};
		gpio.Pin = pinMask();
		gpio.Mode = GPIO_MODE_INPUT;
		gpio.Pull = GPIO_PULLUP;
		gpio.Speed = GPIO_SPEED_FREQ_LOW;

		HAL_GPIO_Init(kern::board::DHT_DATA.port, &gpio);
	}

	uint16_t micros()
	{
		return static_cast<uint16_t>(__HAL_TIM_GET_COUNTER(&htim6));
	}

	uint16_t elapsedUs(uint16_t start)
	{
		return static_cast<uint16_t>(micros() - start);
	}

	void delayUs(uint16_t us)
	{
		uint16_t start = micros();

		while (elapsedUs(start) < us) {
			// Busy wait for a short microsecond delay.
		}
	}

	bool waitForLevel(bool expectedLevel, uint32_t timeoutMs)
	{
		uint32_t startMs = HAL_GetTick();

		while (readPin() != expectedLevel) {
			if ((HAL_GetTick() - startMs) >= timeoutMs) {
				return false;
			}
		}

		return true;
	}

	// The microsecond-critical part of the transaction: sensor response
	// (LOW ~80us, HIGH ~80us) plus 40 data bits of 26-70us pulses.
	// Runs inside a scheduler-suspended window -- must not block long.
	Dht11::Status readResponseAndBits(uint8_t data[5])
	{
		if (!waitForLevel(false, 5)) {
			return Dht11::Status::Timeout;
		}

		if (!waitForLevel(true, 5)) {
			return Dht11::Status::Timeout;
		}

		if (!waitForLevel(false, 5)) {
			return Dht11::Status::Timeout;
		}

		for (uint8_t bitIndex = 0; bitIndex < 40; ++bitIndex) {
			if (!waitForLevel(true, 5)) {
				return Dht11::Status::Timeout;
			}

			uint16_t highStart = micros();

			if (!waitForLevel(false, 5)) {
				return Dht11::Status::Timeout;
			}

			uint16_t highWidth = elapsedUs(highStart);

			uint8_t byteIndex = bitIndex / 8;
			data[byteIndex] <<= 1;

			if (highWidth > 40) {
				data[byteIndex] |= 1;
			}
		}

		return Dht11::Status::Ok;
	}

	} // namespace

	void Dht11::init()
	{
		// TIM6 must run as a free-running 1 us counter.
		HAL_TIM_Base_Start(&htim6);

		// Release the DHT bus. The line idles HIGH through the pull-up.
		setPinOutputOpenDrain();
		HAL_GPIO_WritePin(board::DHT_DATA.port, pinMask(), GPIO_PIN_SET);
	}

	Dht11::Status Dht11::read(float& tempC, float& humidity)
	{
		tempC = 0.0f;
		humidity = 0.0f;

		uint8_t data[5] = {0, 0, 0, 0, 0};

		// Start signal: pull the bus LOW, then release it.
		setPinOutputOpenDrain();
		HAL_GPIO_WritePin(board::DHT_DATA.port, pinMask(), GPIO_PIN_RESET);

		// Standard DHT11 start pulse. This is the only long wait.
		vTaskDelay(pdMS_TO_TICKS(18));

		vTaskSuspendAll();

		HAL_GPIO_WritePin(board::DHT_DATA.port, pinMask(), GPIO_PIN_SET);
		delayUs(30);

		// The sensor now drives the bus.
		setPinInput();

		Status result = readResponseAndBits(data);

		xTaskResumeAll();

		if (result != Status::Ok) {
			return result;
		}

		uint8_t checksum = static_cast<uint8_t>(data[0] + data[1] + data[2] + data[3]);

		if (checksum != data[4]) {
			return Status::CrcError;
		}

		humidity = static_cast<float>(data[0]) + static_cast<float>(data[1]) / 10.0f;
		tempC = static_cast<float>(data[2]) + static_cast<float>(data[3]) / 10.0f;

		return Status::Ok;
	}

} // namespace kern::sensors
