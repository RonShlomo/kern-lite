#pragma once

#include "../recorder/state_machine.hpp"
#include "../recorder/sensor_bus.hpp"
#include "../storage/circular_log.hpp"
#include "../recorder/comm_link.hpp"
#include "../recorder/command_handler.hpp"

namespace kern::system {
	class Orchestrator {
	public:
		void init();
		void runSensorTask();
		void runStorageTask();
		void runCommsTask();
		void runSystemTask();
		static Orchestrator& instance()
		{
			static Orchestrator o;
			return o;
		}

	private:
		recorder::StateMachine sm;
		recorder::SensorBus bus;
		storage::CircularLog box;
		recorder::CommLink link;
		recorder::CommandHandler handler;
	};
}
