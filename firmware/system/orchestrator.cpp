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

			// [Claude] added: anchor to an absolute tick schedule for the duration of this
			// Recording session instead of vTaskDelay's relative delay. vTaskDelay re-measures
			// its 100ms wait from whenever the task happens to resume, so this task's real
			// period silently drifts against the Storage task's independent 100ms timer. Reset
			// fresh every time Recording starts so a long Idle period beforehand can't leave a
			// stale reference that forces a burst of non-delaying catch-up iterations.
			TickType_t lastWake = xTaskGetTickCount();
			// [Claude] added: clear any fault_bits left over from a previous Recording session
			// (or from before the first record of this one is computed below), so the LED can't
			// briefly show a blink for a fault that's no longer real.
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

				// [Claude] added: mirror fault_bits to where runSystemTask's LED logic can see
				// it -- see the member declaration in orchestrator.hpp for why a plain volatile
				// byte is enough here.
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

				// [Claude] changed: the DHT11 poll used to run inside processDigitalSensors()
				// above, before bus.publish(). Dht11::read() blocks for ~18-25ms (its mandatory
				// 18ms start pulse plus the bit-banged response), so on that one tick in twenty
				// this record's publish landed ~20ms late. Running the poll here, after this
				// record is already published and transmitted, removes that blocking call from
				// the critical path -- and SensorBus queuing (see sensor_bus.hpp) means even a
				// late publish no longer risks overwriting a record Storage hasn't read yet.
				pollDht11();

				vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(100));
			}
		}
	}

	void Orchestrator::runStorageTask()
	{
		uint8_t consecutiveWriteFailures = 0;

		for (;;) {
			// [Claude] added: A6.1 hardening -- entering Fault on the 3rd consecutive write
			// failure was already wired up, but Event::FaultCleared (Fault -> Recording in
			// state_machine.cpp) had no caller anywhere, so a Fault was permanent even after the
			// SD card came back. This task owns all SD access, so it's the one that re-probes by
			// calling mount() again; a successful remount fires FaultCleared and resumes
			// Recording with the same m_recSeq sequence (a gap during the fault window, not a
			// reset -- consistent with how a reboot gap is already handled).
			if (sm.isFault()) {
				if (box.mount() == storage::StorageStatus::Ok) {
					sm.process(recorder::Event::FaultCleared);
					handler.sendStatus();
				}

				vTaskDelay(pdMS_TO_TICKS(kern::config::kFaultRemountRetryMs));
				continue;
			}

			// only writes on recording
			if (!sm.isLogging()) {
				consecutiveWriteFailures = 0;
				vTaskDelay(pdMS_TO_TICKS(100));
				continue;
			}

			// [Claude] changed: vTaskDelayUntil keeps this loop on a fixed absolute grid instead
			// of drifting by however long the previous writeRecord() (which calls f_sync()) took.
			// That alone isn't enough to stop record loss, though: a single write that overruns
			// one 100ms period still lets the Sensor task get ahead. SensorBus is now a small FIFO
			// (see sensor_bus.hpp) rather than a single overwritten slot, so the drain loop below
			// catches up on whatever queued up during that overrun instead of losing it.
			TickType_t lastWake = xTaskGetTickCount();

			while (sm.isLogging()) {
				// [Claude] changed: drain every record SensorBus is holding, not just one, each
				// tick. seq == 0 is the "queue empty" sentinel (real records start at seq 1). Also
				// re-checks isLogging() each pass so a mid-drain SdFault (3rd consecutive failure)
				// stops further write attempts immediately, same as before.
				for (storage::SensorRecord rec = bus.latest(); rec.seq != 0 && sm.isLogging(); rec = bus.latest()) {
					const storage::StorageStatus status = box.writeRecord(rec);

					if (status == storage::StorageStatus::Ok) {
						consecutiveWriteFailures = 0;
					} else {
						++consecutiveWriteFailures;

						if (consecutiveWriteFailures >= 3) {
							sm.process(recorder::Event::SdFault);
							consecutiveWriteFailures = 0;
						}
					}
				}

				vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(100));
			}
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
				// [Claude] changed: was an unconditional solid green. Spec calls for blinking
				// green when fault_bits is set (a sensor fault, distinct from the SD-write-fail
				// path that drives the Fault *state* below) -- same 500ms toggle cadence as the
				// Fault LED2_RED blink, just on RGB_G. Falls back to solid the instant
				// m_lastFaultBits clears, regardless of blink phase.
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
	// [Claude] changed: this used to also run the DHT11 hardware poll itself, every 20 ticks,
	// before bus.publish() in runSensorTask(). That poll now happens in pollDht11(), called
	// after publish/transmit -- see the note at that call site. This function now only applies
	// whatever is already known: the cached last-known-good DHT reading, plus any
	// DHT_TIMEOUT/DHT_BADDATA fault the *previous* tick's poll found (one-shot, cleared after
	// being applied once).
	void Orchestrator::processDigitalSensors(kern::storage::SensorRecord& rec, uint8_t& faults)
	{
		faults |= m_pendingDhtFault;
		m_pendingDhtFault = 0;

		// write the last known good values (or 0.0f if never read)
		rec.dht_temp_c = static_cast<int16_t>(m_lastDhtTemp * 10.0);
		rec.dht_hum = static_cast<uint16_t>(m_lastDhtHum * 10.0);
	}

	// [Claude] added: the actual DHT11 hardware poll, split out of processDigitalSensors() (see
	// its note) so it can run after this tick's record is already published/transmitted.
	// Still polled once every 20 ticks (~2s), matching the original cadence.
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
