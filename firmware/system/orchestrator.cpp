#include "orchestrator.hpp"
#include "../hal/gpio.hpp"
#include "../hal/watchdog.hpp"
#include "board.hpp"
#include "FreeRTOS.h"
#include "task.h"
#include "../sensors/lm35.hpp"
#include "../hal/adc.hpp"
#include <cstring>

extern UART_HandleTypeDef huart2;
extern IWDG_HandleTypeDef hiwdg;

namespace kern::system {
	volatile uint16_t debugLm35Raw = 0;
	volatile float debugLm35Volts = 0.0f;
	volatile float debugLm35TempC = 0.0f;

	Orchestrator::Orchestrator()
		: link(&huart2)
	{
	}

	void Orchestrator::init()
	{
		bus.init();
		link.init();
		handler.init(&link);
	}

	void Orchestrator::runSensorTask()
	{

		for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
	}

	void Orchestrator::runStorageTask()
	{
		for (;;) vTaskDelay(pdMS_TO_TICKS(100));
	}

	void Orchestrator::runCommsTask()
	{
		for (;;) {
			kern::protocol::Frame f{};

			if (link.poll(f)) {
				handler.dispatch(f);
			}

			vTaskDelay(pdMS_TO_TICKS(10));
		}
	}

	void Orchestrator::runSystemTask() {
		for (;;) {
			hal::gpio::toggle(board::LED1_BLUE);
			hal::watchdog::kick(hiwdg);
			vTaskDelay(pdMS_TO_TICKS(1000));
		}
	}



}
