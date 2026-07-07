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
		for (;;)
			vTaskDelay(pdMS_TO_TICKS(100));
	}

	void Orchestrator::runStorageTask()
	{
		for (;;) vTaskDelay(pdMS_TO_TICKS(100));
	}

	void Orchestrator::runCommsTask()
	{
	    for (;;) {
	        kern::protocol::Frame f{};

	        if (link.receive(f, pdMS_TO_TICKS(10))) {
	            handler.dispatch(f);

	            while (link.poll(f)) {
	                handler.dispatch(f);
	            }
	        }
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
