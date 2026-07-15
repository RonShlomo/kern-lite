#include "circular_log.hpp"
#include "../protocol/crc32.hpp"
#include "../system/config.hpp" // kStorageLockTimeoutMs

#include <cstdio>
#include <cstring>
#include <cstddef>	 // offsetof()
#include <algorithm> // std::min()

namespace kern::storage
{

	namespace
	{
		class ScopedLock
		{
		public:
			explicit ScopedLock(SemaphoreHandle_t sem) : m_sem(sem)
			{
				m_locked = xSemaphoreTake(m_sem, pdMS_TO_TICKS(kern::config::kStorageLockTimeoutMs)) == pdTRUE;
			}

			~ScopedLock()
			{
				if (m_locked) {
					xSemaphoreGive(m_sem);
				}
			}

			ScopedLock(const ScopedLock &) = delete;
			ScopedLock &operator=(const ScopedLock &) = delete;

			bool locked() const { return m_locked; }

		private:
			SemaphoreHandle_t m_sem;
			bool m_locked;
		};
	} // namespace

	void CircularLog::ensureMutexCreated()
	{
		if (m_mutex == nullptr)
		{
			m_mutex = xSemaphoreCreateMutexStatic(&m_mutexBuffer);
		}
	}

	CircularLog::~CircularLog()
	{
		for (uint8_t i = 0; i < LOG_FILE_COUNT; ++i)
		{
			if (m_filesOpen[i])
			{
				f_close(&m_files[i]);
				m_filesOpen[i] = false;
			}
		}
	}

	// generates file names like "0:LOG00.BIN", "0:LOG01.BIN", etc.
	static void makeLogFilename(uint8_t index, char *outBuffer)
	{
		snprintf(outBuffer, 16, "0:LOG%02u.BIN", index);
	}

	StorageStatus CircularLog::mount()
	{
		ensureMutexCreated();
		ScopedLock guard(m_mutex);
		if (!guard.locked()) {
			return StorageStatus::IoError;
		}

		return mountLocked();
	}

	StorageStatus CircularLog::mountLocked()
	{
		++m_mountAttempts;

		// Mount the FAT filesystem immediately
		FRESULT mountResult = f_mount(&m_fatfs, "0:", 1);
		m_lastMountFResult = static_cast<uint8_t>(mountResult);
		if (mountResult != FR_OK)
		{
			return StorageStatus::IoError;
		}

		for (uint8_t i = 0; i < LOG_FILE_COUNT; ++i)
		{
			char filename[16];
			// generate names like "LOG00.BIN"
			makeLogFilename(i, filename);

			// FA_OPEN_ALWAYS flag means: if the file exists, open it. else, create it now
			FRESULT res = f_open(&m_files[i], filename, FA_READ | FA_WRITE | FA_OPEN_ALWAYS);
			if (res != FR_OK)
			{
				return StorageStatus::IoError;
			}

			f_close(&m_files[i]);
		}

		// try to read the metadata bookmark from META.BIN
		StorageStatus metaStatus = readMeta();

		if (metaStatus == StorageStatus::Ok)
		{
			// Meta is valid. Scan forward from the saved head to find any records
			// written since the last time metadata was saved to the SD card
			recoverPosition();
		}
		else
		{
			// Meta is corrupt or missing. must scan everything to rebuild the state
			recoverPosition();
		}

		// if recoverPosition found nothing (brand new SD card), initialize fresh metadata
		if (m_meta.total_records == 0 && m_meta.version == 0)
		{
			std::memset(&m_meta, 0, sizeof(LogMeta));
			m_meta.magic = META_MAGIC;
			m_meta.version = META_VERSION;
			m_meta.records_per_file = RECORDS_PER_FILE;

			writeMeta();
		}

		// mark system as successfully mounted
		m_mounted = true;
		return StorageStatus::Ok;
	}

	StorageStatus CircularLog::readMeta()
	{
		FIL metaFile{};
		// Open META.BIN
		if (f_open(&metaFile, "0:META.BIN", FA_READ | FA_OPEN_EXISTING) != FR_OK)
		{
			return StorageStatus::NotMounted;
		}

		// read 36 bytes into our m_meta struct
		UINT bytesRead = 0;
		if (f_read(&metaFile, &m_meta, sizeof(LogMeta), &bytesRead) != FR_OK || bytesRead != sizeof(LogMeta))
		{
			f_close(&metaFile);
			return StorageStatus::Corrupt;
		}
		f_close(&metaFile);

		// verify magic number and version
		if (m_meta.magic != META_MAGIC || m_meta.version != META_VERSION)
		{
			return StorageStatus::BadMagic;
		}

		// verify CRC32. calculate CRC over the first 32 bytes (everything before the crc32 field itself)
		uint32_t expectedCrc = protocol::crc32(reinterpret_cast<const uint8_t *>(&m_meta), offsetof(LogMeta, crc32));
		if (m_meta.crc32 != expectedCrc)
		{
			return StorageStatus::Corrupt;
		}

		return StorageStatus::Ok;
	}

	StorageStatus CircularLog::writeMeta()
	{
		// recompute the CRC before saving to the SD card
		m_meta.crc32 = protocol::crc32(reinterpret_cast<const uint8_t *>(&m_meta), offsetof(LogMeta, crc32));

		FIL metaFile{};
		// open or create META.BIN, allowing write access
		if (f_open(&metaFile, "0:META.BIN", FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
		{
			return StorageStatus::IoError;
		}

		// write the 36 bytes of m_meta
		UINT bytesWritten = 0;
		if (f_write(&metaFile, &m_meta, sizeof(LogMeta), &bytesWritten) != FR_OK || bytesWritten != sizeof(LogMeta))
		{
			f_close(&metaFile);
			return StorageStatus::IoError;
		}

		// flush
		f_sync(&metaFile);
		f_close(&metaFile);

		return StorageStatus::Ok;
	}

	// true if 'a' is newer than 'b' in modulo-65536 sequence space
	static bool seqNewer(uint16_t a, uint16_t b)
	{
	    return static_cast<uint16_t>(a - b) < 0x8000;
	}

	// recover position in case of recovering after collapse
	StorageStatus CircularLog::recoverPosition()
	{
		uint16_t newest_seq = 0;
		uint8_t newest_file = 0;
		uint16_t newest_index = 0;
		bool found_any_valid = false;

		for (uint8_t f = 0; f < LOG_FILE_COUNT; ++f)
		{
			char filename[16];
			makeLogFilename(f, filename);

			if (f_open(&m_files[f], filename, FA_READ | FA_OPEN_ALWAYS) != FR_OK)
			{
				continue;
			}
			m_filesOpen[f] = true;

			for (uint16_t i = 0; i < RECORDS_PER_FILE; ++i)
			{
				SensorRecord rec;
				UINT bytes_read;

				// jump to the specific record slot
				f_lseek(&m_files[f], i * sizeof(SensorRecord));
				f_read(&m_files[f], &rec, sizeof(SensorRecord), &bytes_read);

				if (bytes_read == sizeof(SensorRecord))
				{
					// validate the crc to make sure this is not garbage data
					if (recordCrc(rec) == rec.crc32)
					{
						// find the highest sequence number
						if (found_any_valid == false || seqNewer(rec.seq, newest_seq))
						{
							newest_seq = rec.seq;
							newest_file = f;
							newest_index = i;
							found_any_valid = true;
						}
					}
				}
			}

			f_close(&m_files[f]);
			m_filesOpen[f] = false;
		}

		// if we found valid data, set the write head one step after the newest record
		if (found_any_valid == true)
		{
			m_newestSeq = newest_seq;
			m_meta.current_file = newest_file;
			m_meta.write_index = newest_index + 1;

			// handle wrap around if the newest record was exactly at the end of a file
			if (m_meta.write_index >= RECORDS_PER_FILE)
			{
				m_meta.write_index = 0;
				m_meta.current_file = (m_meta.current_file + 1) % LOG_FILE_COUNT;
			}
		}

		return StorageStatus::Ok;
	}

	// record crc calculation
	uint32_t CircularLog::recordCrc(const SensorRecord &r)
	{
		// calculate crc over the first 28 bytes of the record (before crc32 field)
		return protocol::crc32(reinterpret_cast<const uint8_t *>(&r), offsetof(SensorRecord, crc32));
	}

	// write record (save data to the SD card)
	StorageStatus CircularLog::writeRecord(const SensorRecord &r)
	{
		ensureMutexCreated();
		ScopedLock guard(m_mutex);
		if (!guard.locked()) {
			return StorageStatus::IoError;
		}
		// copy the record because the input is const, and calculate its crc
		SensorRecord stored = r;
		stored.crc32 = recordCrc(stored);

		const uint8_t targetFile = m_meta.current_file;
		char filename[16];
		makeLogFilename(targetFile, filename);

		if (f_open(&m_files[targetFile], filename, FA_READ | FA_WRITE | FA_OPEN_ALWAYS) != FR_OK)
		{
			return StorageStatus::IoError;
		}
		m_filesOpen[targetFile] = true;

		// find the correct file and position
		FIL *current_file = &m_files[targetFile];
		f_lseek(current_file, m_meta.write_index * sizeof(SensorRecord));

		// write the 32 bytes to the SD card
		UINT bytes_written = 0;

		FRESULT write_status = f_write(current_file, &stored, sizeof(SensorRecord), &bytes_written);

		f_sync(current_file);
		f_close(current_file);
		m_filesOpen[targetFile] = false;

		if (write_status != FR_OK || bytes_written != sizeof(SensorRecord))
		{
			return StorageStatus::IoError;
		}

		// advance indexes
		++m_meta.write_index;

		// check if the file is full
		if (m_meta.write_index == RECORDS_PER_FILE)
		{
			// reset row
			m_meta.write_index = 0;
			// move to the next file
			m_meta.current_file = (m_meta.current_file + 1) % LOG_FILE_COUNT;

			// if last file was finished, update wrap count
			if (m_meta.current_file == 0)
			{
				++m_meta.wrap_count;
			}
		}

		++m_meta.total_records;

		// save metadata bookmark every 16 records
		if (m_meta.total_records % META_FLUSH_EVERY_N == 0)
		{
			writeMeta();
		}

		return StorageStatus::Ok;
	}

	// replay newest (send history to python)
	StorageStatus CircularLog::replayNewest(uint32_t n, RecordCb cb, void *ctx)
	{
		ensureMutexCreated();
		ScopedLock guard(m_mutex);
		if (!guard.locked()) {
			return StorageStatus::IoError;
		}
		uint32_t total_capacity = LOG_FILE_COUNT * RECORDS_PER_FILE;

		// limit n so we don't try to send records we don't have
		uint32_t available = std::min(m_meta.total_records, total_capacity);
		uint32_t to_replay = std::min(n, available);

		if (to_replay == 0)
		{
			return StorageStatus::Ok;
		}

		// calculate where to start reading from (using modulo)
		uint32_t global_write_pos = m_meta.current_file * RECORDS_PER_FILE + m_meta.write_index;
		uint32_t start_pos = (global_write_pos - to_replay + total_capacity) % total_capacity;

		bool anyFileOpen = false;
		uint8_t openFileIndex = 0;
		StorageStatus result = StorageStatus::Ok;

		// walk forward and send records
		for (uint32_t step = 0; step < to_replay; ++step)
		{
			uint32_t current_pos = (start_pos + step) % total_capacity;
			uint8_t file_index = current_pos / RECORDS_PER_FILE;
			uint16_t rec_index = current_pos % RECORDS_PER_FILE;

			if (!anyFileOpen || file_index != openFileIndex)
			{
				if (anyFileOpen)
				{
					f_close(&m_files[openFileIndex]);
					m_filesOpen[openFileIndex] = false;
					anyFileOpen = false;
				}

				char filename[16];
				makeLogFilename(file_index, filename);

				if (f_open(&m_files[file_index], filename, FA_READ | FA_OPEN_ALWAYS) != FR_OK)
				{
					result = StorageStatus::IoError;
					break;
				}

				m_filesOpen[file_index] = true;
				anyFileOpen = true;
				openFileIndex = file_index;
			}

			SensorRecord rec;
			UINT bytes_read;

			f_lseek(&m_files[file_index], rec_index * sizeof(SensorRecord));
			FRESULT read_status = f_read(&m_files[file_index], &rec, sizeof(SensorRecord), &bytes_read);

			if (read_status != FR_OK || bytes_read != sizeof(SensorRecord))
			{
				result = StorageStatus::IoError;
				break;
			}

			// send the record via callback, if callback returns false, we stop
			// send even if CRC is bad, so python can report the corruption
			if (!cb(rec, ctx))
			{
				// callback failed
				break;
			}
		}

		if (anyFileOpen)
		{
			f_close(&m_files[openFileIndex]);
			m_filesOpen[openFileIndex] = false;
		}

		return result;
	}

	StorageStatus CircularLog::eraseAll(uint32_t magic)
	{
		ensureMutexCreated();
		ScopedLock guard(m_mutex);
		if (!guard.locked()) {
			return StorageStatus::IoError;
		}
		// verify magic == erase magic
		if (magic != ERASE_MAGIC)
		{
			return StorageStatus::BadMagic;
		}

		// close all open files and delete them from the SD
		for (uint8_t i = 0; i < LOG_FILE_COUNT; ++i)
		{
			if (m_filesOpen[i])
			{
				f_close(&m_files[i]);
				m_filesOpen[i] = false;
			}

			char filename[16];
			snprintf(filename, sizeof(filename), "0:LOG%02u.BIN", i);
			f_unlink(filename);
		}

		f_unlink("0:META.BIN");
		m_mounted = false;
		std::memset(&m_meta, 0, sizeof(LogMeta));
		m_newestSeq = 0;

		return mountLocked();
	}

	StorageStatus CircularLog::flushMeta()
	{
		ensureMutexCreated();
		ScopedLock guard(m_mutex);
		if (!guard.locked()) {
			return StorageStatus::IoError;
		}
	    if (!m_mounted) {
	        return StorageStatus::NotMounted;
	    }

	    return writeMeta();
	}

	StatusSnapshot CircularLog::snapshot()
	{
		ensureMutexCreated();
		ScopedLock guard(m_mutex);

		StatusSnapshot s{};

		if (!guard.locked()) {
			return s;
		}
		s.mounted = m_mounted;
		s.currentFile = m_meta.current_file;
		s.writeIndex = m_meta.write_index;
		s.totalRecords = m_meta.total_records;
		s.wrapCount = m_meta.wrap_count;
		return s;
	}

} // namespace kern::storage
