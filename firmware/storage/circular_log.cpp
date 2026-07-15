#include "circular_log.hpp"
#include "../protocol/crc32.hpp"

#include <cstdio>
#include <cstring>
#include <cstddef>	 // offsetof()
#include <algorithm> // std::min()

namespace kern::storage
{

	namespace
	{
		// [Claude] added: RAII helper so every locked method releases the mutex on every return
		// path (there are several early returns below) without needing to remember to call
		// xSemaphoreGive() before each one.
		class ScopedLock
		{
		public:
			// [Claude] added: blocks (forever) until the mutex is free, mirroring the
			// xSemaphoreTake(..., portMAX_DELAY) pattern already used in comm_link.cpp.
			explicit ScopedLock(SemaphoreHandle_t sem) : m_sem(sem)
			{
				xSemaphoreTake(m_sem, portMAX_DELAY);
			}

			// [Claude] added: releases the mutex when the guard goes out of scope.
			~ScopedLock()
			{
				xSemaphoreGive(m_sem);
			}

			// [Claude] added: this guard owns a lock; copying it would double-release.
			ScopedLock(const ScopedLock &) = delete;
			ScopedLock &operator=(const ScopedLock &) = delete;

		private:
			SemaphoreHandle_t m_sem;
		};
	} // namespace

	// [Claude] added: creates the static mutex on first use. Called at the top of every public
	// method below, before constructing a ScopedLock.
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
		// [Claude] added: create-if-needed the mutex, then hold it for the whole mount sequence
		// so it can't interleave with a writeRecord()/flushMeta()/replayNewest()/eraseAll() call
		// running on another task.
		ensureMutexCreated();
		ScopedLock guard(m_mutex);
		// [Claude] added: the actual mount logic now lives in mountLocked() (unchanged below,
		// just moved) so eraseAll() can call it directly while already holding this same lock.
		return mountLocked();
	}

	StorageStatus CircularLog::mountLocked()
	{
		// Mount the FAT filesystem immediately
		if (f_mount(&m_fatfs, "0:", 1) != FR_OK)
		{
			return StorageStatus::IoError;
		}

		// [Claude] changed: _FS_LOCK is configured to 2 (FATFS/Target/ffconf.h), so FatFs can
		// only track 2 simultaneously open file objects across the whole volume. This loop used
		// to leave all 4 log files open in m_files[]/m_filesOpen[] for the entire session, which
		// alone blew that budget. It now just opens each file long enough to confirm it exists
		// (or create it), then closes it immediately -- writeRecord(), replayNewest(), and
		// recoverPosition() below now each open the one file they need on demand and close it
		// before returning, so at most one of these four files is ever open at a time.
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

		// [Claude] changed: opens each file one at a time (scan it, close it, move on) instead of
		// assuming all 4 are already open in m_files[] -- see the note in mountLocked() above on
		// why (_FS_LOCK 2).
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
		// [Claude] added: the Storage task calls this every ~100ms while Recording; take the
		// same lock CMD_STOP/short-press flushMeta() and CMD_STATUS's snapshot() use, so a
		// write can never interleave with a metadata flush or a status read from another task.
		ensureMutexCreated();
		ScopedLock guard(m_mutex);
		// copy the record because the input is const, and calculate its crc
		SensorRecord stored = r;
		stored.crc32 = recordCrc(stored);

		// [Claude] changed: opens the target file for this single write and closes it before
		// returning, instead of writing into a handle left open since mount -- see the note in
		// mountLocked() above on why (_FS_LOCK 2).
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
		// [Claude] added: REPLAY is only dispatched while Idle, so by FSM construction it
		// shouldn't overlap a Recording-only writeRecord() call -- but it still reads the same
		// m_meta/m_files state flushMeta() (short-press, or a queued CMD_STOP) can touch, so it
		// takes the same lock rather than relying on that invariant never changing.
		ensureMutexCreated();
		ScopedLock guard(m_mutex);
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

		// [Claude] changed: keeps at most one of the four files open at a time as the walk
		// crosses file boundaries, closing the previous one before opening the next -- see the
		// note in mountLocked() above on why (_FS_LOCK 2). Previously all 4 were already open in
		// m_files[] from mount, so this loop could just index straight into them.
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
		// [Claude] added: ERASE is Idle-only, so it can't race a writeRecord(), but it does race
		// STATUS reads (any state) and it must not interleave with flushMeta() either.
		ensureMutexCreated();
		ScopedLock guard(m_mutex);
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

		// [Claude] changed: call mountLocked() instead of mount() -- this method already holds
		// the mutex above, and mount() would try to take it again (this mutex is non-recursive,
		// so that call would deadlock this task against itself).
		return mountLocked();
	}

	StorageStatus CircularLog::flushMeta()
	{
		// [Claude] added: this is the method at the center of the race we discussed -- CMD_STOP
		// and the short-press handler both call flushMeta() while the Storage task may still be
		// mid-writeRecord() on another task, so both now go through the same lock.
		ensureMutexCreated();
		ScopedLock guard(m_mutex);
	    if (!m_mounted) {
	        return StorageStatus::NotMounted;
	    }

	    return writeMeta();
	}

	// [Claude] added: single-lock read of every field CommandHandler::sendStatus() needs, so a
	// STATUS frame can no longer be built from a torn mix of pre-wrap/post-wrap values.
	StatusSnapshot CircularLog::snapshot()
	{
		ensureMutexCreated();
		ScopedLock guard(m_mutex);

		StatusSnapshot s{};
		s.mounted = m_mounted;
		s.currentFile = m_meta.current_file;
		s.writeIndex = m_meta.write_index;
		s.totalRecords = m_meta.total_records;
		s.wrapCount = m_meta.wrap_count;
		return s;
	}

} // namespace kern::storage
