#pragma once

#include <cstdint>
#include "../dsp/threshold_detector.hpp" // dsp::ThresholdConfig

namespace kern::config {
	inline constexpr uint32_t kUartBaud = 115200;
	inline constexpr uint32_t kSensorPeriodMs = 100;
	inline constexpr uint32_t kStoragePeriodMs = 100;
	inline constexpr uint32_t kCommsPeriodMs = 10;
	inline constexpr uint32_t kSystemPeriodMs = 50;
	inline constexpr uint32_t kMetaFlushEveryN = 16;
	// Bounds every CircularLog mutex wait. Comms/System-task calls (flushMeta() on STOP/
	// short-press, snapshot() on the heartbeat) used to block forever if the Storage task held
	// the lock through a stalled SD write, which could starve runSystemTask's IWDG kick past
	// the ~4s hardware timeout (IWDG_PRESCALER_256, Reload=499). 1s keeps every caller well
	// under that budget while staying generous next to a normal <100ms write+f_sync.
	inline constexpr uint32_t kStorageLockTimeoutMs = 1000;
	// How often runStorageTask() re-probes the SD card (via CircularLog::mount()) while in
	// Fault, looking for FaultCleared. Not so fast it hammers a card that's still bad, not so
	// slow that a real recovery (card reseated, power-cycled) sits undetected for long.
	inline constexpr uint32_t kFaultRemountRetryMs = 2000;
	inline constexpr uint8_t kLogFileCount = 4;
	inline constexpr uint16_t kRecordsPerFile = 256;
	inline constexpr uint32_t kEraseMagic = 0xDEADC0DEu;

	// moving average window size
	inline constexpr uint32_t kDspWindow = 16;

	// DSP Threshold Configurations for Sensor Channels {lo, hi, hysteresis}
	inline constexpr dsp::ThresholdConfig kThresholdLm35 = {10.0f, 40.0f, 2.0f};
	inline constexpr dsp::ThresholdConfig kThresholdPhoto = {0.05f, 0.95f, 0.05f};
	inline constexpr dsp::ThresholdConfig kThresholdPot = {0.02f, 0.98f, 0.02f};
	inline constexpr dsp::ThresholdConfig kThresholdDht11Temp = {5.0f, 45.0f, 2.0f};
	inline constexpr dsp::ThresholdConfig kThresholdDht11Hum = {10.0f, 90.0f, 5.0f};
}
