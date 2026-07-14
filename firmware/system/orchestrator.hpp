#pragma once

#include "../recorder/state_machine.hpp"
#include "../recorder/sensor_bus.hpp"
#include "../storage/circular_log.hpp"
#include "../recorder/comm_link.hpp"
#include "../recorder/command_handler.hpp"
#include "../dsp/channel.hpp"
#include "../system/config.hpp" // kDspWindow (moving average constant)
#include "../storage/sensor_record.hpp"
#include "../protocol/frame.hpp"
#include "../protocol/crc32.hpp"
#include "../sensors/lm35.hpp"
#include "../sensors/photodiode.hpp"
#include "../sensors/potentiometer.hpp"
#include "../sensors/dht11.hpp"
#include "../sensors/buttons.hpp"

namespace kern::system {
	class Orchestrator {
	public:
		Orchestrator();
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
		// helper functions
		void processAnalogSensors(kern::storage::SensorRecord& rec, uint8_t& current_faults);
		void processDigitalSensors(kern::storage::SensorRecord& rec, uint8_t& current_faults);
		void evaluateAlerts(uint8_t& current_alerts);
		void transmitRecord(const kern::storage::SensorRecord& rec);


		recorder::StateMachine sm;
		recorder::SensorBus bus;
		storage::CircularLog box;
		recorder::CommLink link;
		recorder::CommandHandler handler;

		// state variables
		uint16_t m_recSeq = 0;
		uint32_t m_dhtTickCount = 0;
		float m_lastDhtTemp = 0.0f;
		float m_lastDhtHum = 0.0f;

		// DSP channels parameterised with the window size from config
		kern::dsp::Channel<kern::config::kDspWindow> chLm35;
		kern::dsp::Channel<kern::config::kDspWindow> chPhoto;
		kern::dsp::Channel<kern::config::kDspWindow> chPot;

		// physical sensor instances
		kern::sensors::Lm35 m_sensorLm35;
		kern::sensors::Photodiode m_sensorPhoto;
		kern::sensors::Potentiometer m_sensorPot;
		kern::sensors::Dht11 m_sensorDht11;
		kern::sensors::Buttons m_buttons;
	};
}
