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
#include <atomic>

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

		bool storageIsIdle() const
		{
		    return (!m_sensorBusy.load() && bus.pendingCount() == 0 && !m_storageBusy.load());
		}
	private:
		// helper functions
		void processAnalogSensors(kern::storage::SensorRecord& rec, uint8_t& current_faults);
		void processDigitalSensors(kern::storage::SensorRecord& rec, uint8_t& current_faults);
		void evaluateAlerts(uint8_t& current_alerts);
		void transmitRecord(const kern::storage::SensorRecord& rec);
		// [Claude] added: the actual DHT11 hardware poll, split out of processDigitalSensors()
		// and called after bus.publish()/transmitRecord() in runSensorTask(). See the call site
		// for why.
		void pollDht11();

		std::atomic<bool> m_storageBusy{false};
		std::atomic<bool> m_sensorBusy{false};

		recorder::StateMachine sm;
		recorder::SensorBus bus;
		storage::CircularLog box;
		recorder::CommLink link;
		recorder::CommandHandler handler;

		// state variables
		uint16_t m_recSeq = 0;
		// [Claude] added: A6.1 LED hardening -- runSensorTask() computes fault_bits fresh into
		// each SensorRecord every tick, but nothing outside that function could see it, so
		// runSystemTask's LED switch had no way to distinguish Recording+nominal from
		// Recording+fault. Plain volatile byte, same pattern as the debug* globals above:
		// single-word loads/stores are atomic on Cortex-M, so no mutex is needed for one task to
		// write it and another to read it.
		volatile uint8_t m_lastFaultBits = 0;
		uint32_t m_dhtTickCount = 0;
		float m_lastDhtTemp = 0.0f;
		float m_lastDhtHum = 0.0f;
		// [Claude] added: a DHT_TIMEOUT/DHT_BADDATA fault detected by pollDht11() is applied to
		// the *next* record's fault_bits (see processDigitalSensors()), since the poll now runs
		// after the record it would have applied to is already published.
		uint8_t m_pendingDhtFault = 0;

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
