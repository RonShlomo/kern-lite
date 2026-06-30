#include "tasks.hpp"
#include "tasks_defs.hpp"
#include "orchestrator.hpp"
#include "FreeRTOS.h"
#include "task.h"

using O = kern::system::Orchestrator;

static StaticTask_t sensorTcb, storageTcb, commsTcb, systemTcb;
static StackType_t sensorStack[kern::tasks::kSensorStack];
static StackType_t storageStack[kern::tasks::kStorageStack];
static StackType_t commsStack[kern::tasks::kCommsStack];
static StackType_t systemStack[kern::tasks::kSystemStack];

static void sensorTask(void*)
{
	O::instance().runSensorTask();
}

static void storageTask(void*)
{
	O::instance().runStorageTask();
}

static void commsTask(void*)
{
	O::instance().runCommsTask();
}

static void systemTask(void*)
{
	O::instance().runSystemTask();
}

void kern_create_tasks() {
	O::instance().init();

	xTaskCreateStatic(sensorTask, "Sensor", kern::tasks::kSensorStack, nullptr,
			kern::tasks::kSensorPrio, sensorStack, &sensorTcb);

	xTaskCreateStatic(storageTask, "Storage", kern::tasks::kStorageStack, nullptr,
			kern::tasks::kStoragePrio, storageStack, &storageTcb);

	xTaskCreateStatic(commsTask, "Comms", kern::tasks::kCommsStack, nullptr,
			kern::tasks::kCommsPrio, commsStack, &commsTcb);

	xTaskCreateStatic(systemTask, "System", kern::tasks::kSystemStack, nullptr,
			kern::tasks::kSystemPrio, systemStack, &systemTcb);
}

extern "C" {
void vApplicationGetIdleTaskMemory(StaticTask_t** tcb, StackType_t** stack, uint32_t* size) {
	static StaticTask_t idleTcb;
	static StackType_t idleStack[configMINIMAL_STACK_SIZE];
	*tcb=&idleTcb; *stack=idleStack; *size=configMINIMAL_STACK_SIZE;
}

void vApplicationGetTimerTaskMemory(StaticTask_t** tcb, StackType_t** stack, uint32_t* size) {
	static StaticTask_t timerTcb;
	static StackType_t timerStack[configTIMER_TASK_STACK_DEPTH];
	*tcb=&timerTcb; *stack=timerStack; *size=configTIMER_TASK_STACK_DEPTH;
}
}
