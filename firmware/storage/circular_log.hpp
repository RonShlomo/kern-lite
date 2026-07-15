#pragma once

#include "sensor_record.hpp"
#include "ff.h" // Required for FATFS and FIL types
// [Claude] added: FreeRTOS.h/semphr.h declare the static-mutex types (StaticSemaphore_t,
// SemaphoreHandle_t) and primitives (xSemaphoreCreateMutexStatic/Take/Give) used below to
// serialize access to this object across the Sensor/Storage task and the Comms/System tasks.
#include "FreeRTOS.h"
#include "semphr.h"
#include <cstdint>

namespace kern::storage
{

	constexpr uint8_t LOG_FILE_COUNT = 4;
	constexpr uint16_t RECORDS_PER_FILE = 256;
	constexpr uint16_t META_FLUSH_EVERY_N = 16;
	constexpr uint32_t ERASE_MAGIC = 0xDEADC0DEu;
	constexpr uint32_t META_MAGIC = 0x4C4F4700u; // "LOG\0"
	constexpr uint32_t META_VERSION = 1u;

#pragma pack(push, 1)
	struct LogMeta
	{ // stored in META.BIN
		uint32_t magic;
		uint32_t version;
		uint8_t file_count;
		uint16_t records_per_file;
		uint8_t current_file;
		uint16_t write_index;
		uint32_t wrap_count;
		uint32_t total_records;
		uint8_t reserved[10]; // pad to 32 bytes before CRC
		uint32_t crc32;		  // over all preceding bytes
	};
#pragma pack(pop)

	static_assert(sizeof(LogMeta) == 36, "LogMeta size");

	enum class StorageStatus : uint8_t
	{
		Ok,
		IoError,
		Corrupt,
		Full,
		BadMagic,
		NotMounted
	};

	using RecordCb = bool (*)(const SensorRecord &, void *ctx);

	// [Claude] added: a single, lock-consistent view of the fields CommandHandler::sendStatus()
	// needs. Previously sendStatus() called currentFile()/writeIndex()/totalRecords()/
	// wrapCount()/isMounted() as five separate unlocked reads, which could observe a torn mix
	// of pre-wrap and post-wrap values if the Storage task was mid-writeRecord() at that exact
	// instant. snapshot() takes the same mutex writeRecord() takes, so every field it returns
	// reflects one consistent point in time.
	struct StatusSnapshot
	{
		bool mounted;
		uint8_t currentFile;
		uint16_t writeIndex;
		uint32_t totalRecords;
		uint32_t wrapCount;
	};

	class CircularLog
	{
	public:
		~CircularLog();

		StorageStatus mount();
		StorageStatus writeRecord(const SensorRecord &r);
		StorageStatus replayNewest(uint32_t n, RecordCb cb, void *ctx);
		StorageStatus eraseAll(uint32_t magic);
		StorageStatus flushMeta();

		// [Claude] added: thread-safe replacement for reading multiple STATUS fields individually.
		StatusSnapshot snapshot();

		uint16_t newestSeq() const { return m_newestSeq; }
		uint32_t totalRecords() const { return m_meta.total_records; }
		uint32_t wrapCount() const { return m_meta.wrap_count; }
		uint8_t currentFile() const { return m_meta.current_file; }
		uint16_t writeIndex() const { return m_meta.write_index; }
		bool isMounted() const { return m_mounted; }

		// [Claude] added: A6.1 fault-recovery diagnosis -- surfaces exactly what f_mount()
		// returns on each mount/remount attempt, and how many attempts have happened, over the
		// wire via sendStatus(). Added because live-debugging this (breakpoints in
		// user_diskio_spi.c) turned out to perturb the SD subsystem's behavior itself, making
		// debugger-based traces unreliable. volatile: written by whichever task calls
		// mount()/mountLocked() (Storage task during Fault retries, or startup), read from the
		// Comms/System task via sendStatus(); single-word members so no lock is needed.
		uint32_t mountAttempts() const { return m_mountAttempts; }
		uint8_t lastMountFResult() const { return m_lastMountFResult; }

	private:
		StorageStatus readMeta();
		StorageStatus writeMeta();
		StorageStatus recoverPosition();
		uint32_t metaCrc(const LogMeta &m);
		uint32_t recordCrc(const SensorRecord &r);
		// [Claude] added: mount()'s original body, extracted verbatim so eraseAll() can re-run
		// it while already holding the lock, without taking the (non-recursive) mutex twice
		// from the same task -- that would deadlock.
		StorageStatus mountLocked();
		// [Claude] added: lazily creates the static mutex on first use (mount()/writeRecord()/etc.
		// are all reachable before any explicit "init()" step exists on this class). Safe to call
		// every time; it only creates the semaphore once.
		void ensureMutexCreated();

		uint16_t m_newestSeq = 0;

		FATFS m_fatfs{};
		FIL m_files[LOG_FILE_COUNT]{};
		bool m_filesOpen[LOG_FILE_COUNT]{};
		LogMeta m_meta{};
		bool m_mounted = false;

		volatile uint32_t m_mountAttempts = 0;
		volatile uint8_t m_lastMountFResult = 0;

		// [Claude] added: guards every method that touches m_meta or the SD card. FatFs/SPI1 is a
		// single shared peripheral, so without this, the Sensor/Storage task and the Comms/System
		// task (via CommandHandler's flushMeta()/replayNewest()/eraseAll() calls) could issue
		// concurrent FatFs calls and interleave SPI transactions, not just read stale fields.
		StaticSemaphore_t m_mutexBuffer{};
		SemaphoreHandle_t m_mutex = nullptr;
	};

} // namespace kern::storage
