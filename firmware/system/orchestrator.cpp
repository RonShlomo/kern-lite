#include "orchestrator.hpp"
#include "../hal/gpio.hpp"
#include "../hal/watchdog.hpp"
#include "board.hpp"
#include "FreeRTOS.h"
#include "task.h"
#include <cstring>

extern UART_HandleTypeDef huart2;
extern IWDG_HandleTypeDef hiwdg;

namespace kern::system {
	void Orchestrator::init()
	{
		bus.init();
	}

	void Orchestrator::runSensorTask()
	{
		for (;;)
			vTaskDelay(pdMS_TO_TICKS(100));
	}

	void Orchestrator::runStorageTask()
	{
		for (;;) vTaskDelay(pdMS_TO_TICKS(100));
	}

	void Orchestrator::runCommsTask()
	{
		for (;;) vTaskDelay(pdMS_TO_TICKS(10));
	}

	void Orchestrator::runSystemTask() {
		static const char msg[] = "KERN-LITE ALIVE\r\n";
		for (;;) {
			hal::gpio::toggle(board::LED1_BLUE);
			HAL_UART_Transmit(&huart2, reinterpret_cast<const uint8_t*>(msg), sizeof(msg)-1, 100);
			hal::watchdog::kick(hiwdg);
			vTaskDelay(pdMS_TO_TICKS(1000));
		}
	}
}
