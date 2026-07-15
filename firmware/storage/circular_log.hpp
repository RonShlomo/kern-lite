#pragma once

#include "sensor_record.hpp"
#include "ff.h" // Required for FATFS and FIL types
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

	// a single, lock-consistent view of the fields CommandHandler::sendStatus() needs.
	// snapshot() takes the same mutex writeRecord() takes, so every field it returns
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

		StatusSnapshot snapshot();

		uint16_t newestSeq() const { return m_newestSeq; }
		uint32_t totalRecords() const { return m_meta.total_records; }
		uint32_t wrapCount() const { return m_meta.wrap_count; }
		uint8_t currentFile() const { return m_meta.current_file; }
		uint16_t writeIndex() const { return m_meta.write_index; }
		bool isMounted() const { return m_mounted; }

		uint32_t mountAttempts() const { return m_mountAttempts; }
		uint8_t lastMountFResult() const { return m_lastMountFResult; }

	private:
		StorageStatus readMeta();
		StorageStatus writeMeta();
		StorageStatus recoverPosition();
		uint32_t metaCrc(const LogMeta &m);
		uint32_t recordCrc(const SensorRecord &r);
		StorageStatus mountLocked();
		void ensureMutexCreated();

		uint16_t m_newestSeq = 0;

		FATFS m_fatfs{};
		FIL m_files[LOG_FILE_COUNT]{};
		bool m_filesOpen[LOG_FILE_COUNT]{};
		LogMeta m_meta{};
		bool m_mounted = false;

		volatile uint32_t m_mountAttempts = 0;
		volatile uint8_t m_lastMountFResult = 0;

		StaticSemaphore_t m_mutexBuffer{};
		SemaphoreHandle_t m_mutex = nullptr;
	};

} // namespace kern::storage
