#include "orchestrator.hpp"
#include "../storage/sensor_record.hpp" // for SensorRecord struct
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
		handler.init(&link, &sm, &box);
		box.mount();
		m_recSeq = box.newestSeq();


		m_buttons.init();
		// initialize DSP channels thresholds on system startup
		chLm35.configure(kern::config::kThresholdLm35);
		chPhoto.configure(kern::config::kThresholdPhoto);
		chPot.configure(kern::config::kThresholdPot);
	}

	void Orchestrator::runSensorTask()
	{
		for (;;) {
			// only works on recording
			if (!sm.isLogging()) {
				vTaskDelay(pdMS_TO_TICKS(100));
				continue;
			}

			TickType_t lastWake = xTaskGetTickCount();

			m_lastFaultBits = 0;

			while (sm.isLogging()) {
				kern::storage::SensorRecord rec{};
				uint8_t current_faults = 0;
				uint8_t current_alerts = 0;

				// when and where this specific record is collected
				rec.timestamp = HAL_GetTick() / 1000;
				rec.ms = HAL_GetTick() % 1000;
				rec.seq = ++m_recSeq;
				rec.state = static_cast<uint8_t>(sm.state());

				// collect and process data
				processAnalogSensors(rec, current_faults);
				processDigitalSensors(rec, current_faults);
				evaluateAlerts(current_alerts);

				// store the aggregated error and warning flags into the record
				rec.fault_bits = current_faults;
				rec.alert_bits = current_alerts;

				m_lastFaultBits = current_faults;

				// error detection (crc3 protocol)
				rec.crc32 = kern::protocol::crc32(
						reinterpret_cast<const uint8_t*>(&rec),
						offsetof(kern::storage::SensorRecord, crc32)
				);

				// the record is now complete and verified. now we distribute this record to downstream consumers
				// SensorBus: Internal RTOS bus/queue (for the Storage Task to save to SD card).
				// CommLink: External UART connection (for the Ground Station GUI).
				bus.publish(rec);

				transmitRecord(rec);

				pollDht11();

				vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(100));
			}

		}
	}

	void Orchestrator::runStorageTask()
	{

		uint8_t consecutiveWriteFailures = 0;
		storage::SensorRecord pendingRecord{};
		bool hasPendingRecord = false;

		for (;;) {
			if (sm.isFault()) {
				if (box.mount() == storage::StorageStatus::Ok) {
					sm.process(recorder::Event::FaultCleared);
					handler.sendStatus();
				}

				vTaskDelay(pdMS_TO_TICKS(kern::config::kFaultRemountRetryMs));
				continue;
			}

			if (!hasPendingRecord) {
				if (!bus.receive(pendingRecord, pdMS_TO_TICKS(100))) {
					continue;
				}

				hasPendingRecord = true;
				m_storageBusy.store(true);
			}

			const storage::StorageStatus status =
					box.writeRecord(pendingRecord);

			if (status == storage::StorageStatus::Ok) {
				hasPendingRecord = false;
				m_storageBusy.store(false);
				consecutiveWriteFailures = 0;
				continue;
			}

			++consecutiveWriteFailures;

			if (consecutiveWriteFailures >= 3) {
				sm.process(recorder::Event::SdFault);
				consecutiveWriteFailures = 0;
			}

			vTaskDelay(pdMS_TO_TICKS(100));
		}
	}

	void Orchestrator::runCommsTask()
	{
		for (;;) {
			kern::protocol::Frame f{};

			while (link.poll(f)) {
				handler.dispatch(f);
			}

			vTaskDelay(pdMS_TO_TICKS(10));
		}
	}

	void Orchestrator::runSystemTask() {

		TickType_t lastWake = xTaskGetTickCount();
		uint8_t halfSecondCounter = 0;
		uint8_t heartbeatCounter = 0;

		hal::gpio::clear(board::LED1_BLUE);
		hal::gpio::clear(board::RGB_G);
		hal::gpio::clear(board::LED2_RED);

		for (;;) {
			hal::watchdog::kick(hiwdg);

			if (m_buttons.pollSw1() == sensors::PressType::Short) {
				if (sm.process(recorder::Event::ShortPress)) {
					box.flushMeta();
					handler.sendStatus();
				}
			}

			++halfSecondCounter;

			++heartbeatCounter;
			if (heartbeatCounter >= 100) {
				heartbeatCounter = 0;
				handler.sendStatus();
			}

			if (halfSecondCounter >= 10) {
				halfSecondCounter = 0;
			}

			switch (sm.state()) {

			case recorder::State::Idle:
				hal::gpio::clear(board::RGB_G);
				hal::gpio::clear(board::LED2_RED);
				break;

			case recorder::State::Recording:
				if (m_lastFaultBits != 0) {
					if (halfSecondCounter == 0) {
						hal::gpio::toggle(board::RGB_G);
					}
				} else {
					hal::gpio::set(board::RGB_G);
				}
				hal::gpio::clear(board::LED2_RED);
				break;

			case recorder::State::Fault:
				hal::gpio::clear(board::RGB_G);

				if (halfSecondCounter == 0) {
					hal::gpio::toggle(board::LED2_RED);
				}
				break;
			}

			vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(50));
		}
	}


	// --------- helper functions ---------

	// Reads raw values from sensors, performs hardware bounds checking to detect faults,
	// passes the raw values through the DSP channel which applies the moving avrg filter and update alerts state
	void Orchestrator::processAnalogSensors(kern::storage::SensorRecord& rec, uint8_t& faults)
	{
		// read LM35 temperature sensor
		float tempC = m_sensorLm35.readCelsius();
		// fault check
		if (tempC < -10.0f || tempC > 100.0f) {
			faults |= kern::storage::kFaultLm35Range;
		}

		auto outLm35 = chLm35.process(tempC);
		rec.lm35_c = static_cast<int16_t>(outLm35.filtered * 10.0f);

		// read photodiode light sensor
		float lightRaw = m_sensorPhoto.readNormalized();
		if (lightRaw <= 0.001f || lightRaw >= 0.999f) {
			faults |= kern::storage::kFaultLightStuck;
		}

		auto outPhoto = chPhoto.process(lightRaw);
		rec.light = static_cast<uint16_t>(outPhoto.filtered * 65535.0f);

		// Potentiometer
		float potRaw = m_sensorPot.readPercent();
		if (potRaw <= 0.001f || potRaw >= 0.999f) {
			faults |= kern::storage::kFaultPotStuck;
		}

		auto outPot = chPot.process(potRaw);
		rec.pot = static_cast<uint16_t>(outPot.filtered * 65535.0f);
	}


	// process digital sensors
	void Orchestrator::processDigitalSensors(kern::storage::SensorRecord& rec, uint8_t& faults)
	{
		faults |= m_pendingDhtFault;
		m_pendingDhtFault = 0;

		// write the last known good values (or 0.0f if never read)
		rec.dht_temp_c = static_cast<int16_t>(m_lastDhtTemp * 10.0);
		rec.dht_hum = static_cast<uint16_t>(m_lastDhtHum * 10.0);
	}

	void Orchestrator::pollDht11()
	{
		if (++m_dhtTickCount < 20) {
			return;
		}
		m_dhtTickCount = 0;

		float dhtT = 0.0f, dhtH = 0.0f;

		auto status = m_sensorDht11.read(dhtT, dhtH);

		if (status == kern::sensors::Dht11::Status::Timeout) {
			m_pendingDhtFault = kern::storage::kFaultDhtTimeout;
		} else if (status == kern::sensors::Dht11::Status::CrcError) {
			m_pendingDhtFault = kern::storage::kFaultDhtBadData;
		} else {
			m_lastDhtTemp = dhtT;
			m_lastDhtHum = dhtH;
		}
	}


	// evaluate alerts
	// aggregates the alert state from all DSP channels into a single bitmask
	void Orchestrator::evaluateAlerts(uint8_t& alerts)
	{
		// check LM35 bounds
		if (chLm35.alert() == kern::dsp::ThresholdDetector::State::HighAlert) {
			alerts |= (1 << 0);
		} else if (chLm35.alert() == kern::dsp::ThresholdDetector::State::LowAlert) {
			alerts |= (1 << 1);
		}

		// check photodiode bounds
		if (chPhoto.alert() == kern::dsp::ThresholdDetector::State::HighAlert) {
			alerts |= (1 << 2);
		} else if (chPhoto.alert() == kern::dsp::ThresholdDetector::State::LowAlert) {
			alerts |= (1 << 3);
		}

		// check potentiometer bounds
		if (chPot.alert() == kern::dsp::ThresholdDetector::State::HighAlert) {
			alerts |= (1 << 4);
		} else if (chPot.alert() == kern::dsp::ThresholdDetector::State::LowAlert) {
			alerts |= (1 << 5);
		}
	}

	// transmits the assembled record over UART as a live telemetry frame
	void Orchestrator::transmitRecord(const kern::storage::SensorRecord& rec)
	{
		kern::protocol::Frame outFrame{};
		outFrame.type = kern::protocol::FrameType::Record;
		outFrame.len = sizeof(kern::storage::SensorRecord);
		std::memcpy(outFrame.payload, &rec, sizeof(kern::storage::SensorRecord));

		link.send(outFrame);
	}


} // end namespace kern::system
