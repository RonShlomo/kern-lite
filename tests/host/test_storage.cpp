#include "../../firmware/storage/circular_log.hpp"
#include <cstdio>
#include <cassert>
#include <cstring>
#include <iostream>

using namespace kern::storage;

// 1. FATFS MOCK (The Shim) - IMPLEMENTATIONS ONLY
extern "C"
{
    // Fake f_mount (does nothing on PC) - Names omitted to prevent warnings
    FRESULT f_mount(FATFS * /*fs*/, const char * /*path*/, uint8_t /*opt*/)
    {
        return FR_OK;
    }

    // Fake f_open using standard fopen
    FRESULT f_open(FIL *fp, const char *path, uint8_t mode)
    {
        // If this FIL is already open, close the old handle first
        if (fp->pc_file)
        {
            fclose(fp->pc_file);
            fp->pc_file = nullptr;
        }

        // Strip "0:" from the path for the PC file system
        const char *clean_path = (path[0] == '0' && path[1] == ':') ? path + 2 : path;

        if (mode & FA_CREATE_ALWAYS)
        {
            fp->pc_file = fopen(clean_path, "wb+");
        }
        else
        {
            // Try "rb+" (Read/Update). This allows random access read/write WITHOUT truncating.
            fp->pc_file = fopen(clean_path, "rb+");

            // If it failed (file doesn't exist) and FA_OPEN_ALWAYS is set, create it
            if (!fp->pc_file && (mode & FA_OPEN_ALWAYS))
            {
                fp->pc_file = fopen(clean_path, "wb+");
            }
        }
        return fp->pc_file ? FR_OK : FR_DISK_ERR;
    }

    FRESULT f_close(FIL *fp)
    {
        if (fp->pc_file)
        {
            fclose(fp->pc_file);
            fp->pc_file = nullptr;
        }
        return FR_OK;
    }

    FRESULT f_read(FIL *fp, void *buff, UINT btr, UINT *br)
    {
        *br = fread(buff, 1, btr, fp->pc_file);
        return FR_OK;
    }

    FRESULT f_write(FIL *fp, const void *buff, UINT btw, UINT *bw)
    {
        *bw = fwrite(buff, 1, btw, fp->pc_file);
        fflush(fp->pc_file);
        return FR_OK;
    }

    FRESULT f_lseek(FIL *fp, uint32_t ofs)
    {
        fseek(fp->pc_file, ofs, SEEK_SET);
        return FR_OK;
    }

    FRESULT f_sync(FIL *fp)
    {
        fflush(fp->pc_file);
        return FR_OK;
    }

    FRESULT f_unlink(const char *path)
    {
        const char *clean_path = (path[0] == '0' && path[1] == ':') ? path + 2 : path;
        remove(clean_path);
        return FR_OK;
    }
}

// HELPER FUNCTION TO GENERATE DUMMY RECORDS
SensorRecord createDummyRecord(uint32_t seq)
{
    SensorRecord r{};
    r.seq = seq;
    r.timestamp = 1000 + seq;
    r.lm35_c = 250;
    return r;
}

// REPLAY CALLBACK (For testing replayNewest)
struct ReplayContext
{
    uint32_t count;
    uint32_t expected_seq;
};

// This callback is called by replayNewest for every record it reads
bool test_cb(const SensorRecord &r, void *ctx)
{
    ReplayContext *rc = static_cast<ReplayContext *>(ctx);

    // Assert that the records are arriving in perfect chronological order
    assert(r.seq == rc->expected_seq);

    rc->expected_seq++;
    rc->count++;
    return true; // continue reading
}

// THE TESTS

void test_fill_single_file()
{
    std::cout << "Running test_fill_single_file... ";

    CircularLog log;
    log.eraseAll(ERASE_MAGIC); // Clean start
    // log.mount();

    // Write exactly one file full of records (256)
    for (uint32_t i = 0; i < RECORDS_PER_FILE; ++i)
    {
        log.writeRecord(createDummyRecord(i));
    }

    // Verify current file advanced to 1, and index reset to 0
    assert(log.currentFile() == 1);
    assert(log.writeIndex() == 0);
    assert(log.wrapCount() == 0);

    std::cout << "PASSED\n";
}

void test_full_wrap_around()
{
    std::cout << "Running test_full_wrap_around... ";

    CircularLog log;
    log.eraseAll(ERASE_MAGIC);
    // log.mount();

    // Write exactly enough records to fill ALL 4 files (4 * 256 = 1024)
    uint32_t total_capacity = LOG_FILE_COUNT * RECORDS_PER_FILE;
    for (uint32_t i = 0; i < total_capacity; ++i)
    {
        log.writeRecord(createDummyRecord(i));
    }

    // Verify we looped back to file 0, index 0, and wrap_count is 1
    assert(log.currentFile() == 0);
    assert(log.writeIndex() == 0);
    assert(log.wrapCount() == 1);

    std::cout << "PASSED\n";
}

void test_overwrite_after_wrap()
{
    std::cout << "Running test_overwrite_after_wrap... ";

    CircularLog log;
    log.eraseAll(ERASE_MAGIC);
    // log.mount();

    uint32_t total_capacity = LOG_FILE_COUNT * RECORDS_PER_FILE;

    // Fill all files
    for (uint32_t i = 0; i < total_capacity; ++i)
    {
        log.writeRecord(createDummyRecord(i));
    }

    // Write ONE more record (seq = 1024)
    log.writeRecord(createDummyRecord(total_capacity));

    // Verify pointers moved correctly
    assert(log.currentFile() == 0);
    assert(log.writeIndex() == 1);

    // Now, manually open LOG00.BIN and check if the oldest record was overwritten
    FILE *f = fopen("LOG00.BIN", "rb");
    assert(f != nullptr);

    SensorRecord stored;
    fread(&stored, sizeof(SensorRecord), 1, f);
    fclose(f);

    // The first slot should now contain our NEWEST record (seq = 1024), not seq 0
    assert(stored.seq == total_capacity);

    std::cout << "PASSED\n";
}

void test_replay_10_of_15()
{
    std::cout << "Running test_replay_10_of_15... ";
    CircularLog log;
    log.eraseAll(ERASE_MAGIC);
    // log.mount();

    for (uint32_t i = 0; i < 15; ++i)
    {
        log.writeRecord(createDummyRecord(i));
    }

    // We wrote 15 (seqs 0-14). We want the newest 10.
    // So we expect sequences 5 through 14.
    ReplayContext ctx = {0, 5};
    log.replayNewest(10, test_cb, &ctx);

    assert(ctx.count == 10);
    std::cout << "PASSED\n";
}

void test_replay_400_of_300()
{
    std::cout << "Running test_replay_400_of_300... ";
    CircularLog log;
    log.eraseAll(ERASE_MAGIC);
    // log.mount();

    for (uint32_t i = 0; i < 300; ++i)
    {
        log.writeRecord(createDummyRecord(i));
    }

    // We ask for 400, but only 300 exist. It should clamp and return exactly 300.
    ReplayContext ctx = {0, 0};
    log.replayNewest(400, test_cb, &ctx);

    assert(ctx.count == 300);
    std::cout << "PASSED\n";
}

void test_replay_crossing_boundary()
{
    std::cout << "Running test_replay_crossing_boundary... ";
    CircularLog log;
    log.eraseAll(ERASE_MAGIC);
    // log.mount();

    // Write 261 records (File 0 is full with 256, File 1 has 5)
    for (uint32_t i = 0; i < 261; ++i)
    {
        log.writeRecord(createDummyRecord(i));
    }

    // We want the newest 20. 261 - 20 = 241.
    // It should transparently cross from LOG00.BIN to LOG01.BIN
    ReplayContext ctx = {0, 241};
    log.replayNewest(20, test_cb, &ctx);

    assert(ctx.count == 20);
    std::cout << "PASSED\n";
}

void test_erase_bad_magic()
{
    std::cout << "Running test_erase_bad_magic... ";
    CircularLog log;
    log.mount();

    // Try to erase with wrong magic word
    StorageStatus st = log.eraseAll(0x00000000);

    assert(st == StorageStatus::BadMagic);
    assert(log.isMounted() == true); // State should be unchanged
    std::cout << "PASSED\n";
}

void test_recovery_and_meta_corruption()
{
    std::cout << "Running test_recovery_and_meta_corruption... ";
    CircularLog log;
    log.eraseAll(ERASE_MAGIC);
    // log.mount();

    for (uint32_t i = 0; i < 40; ++i)
    {
        log.writeRecord(createDummyRecord(i));
    }

    FILE *f_meta = fopen("META.BIN", "rb+");
    uint8_t bad_byte = 0xFF;
    fwrite(&bad_byte, 1, 1, f_meta);
    fclose(f_meta);

    FILE *f_log = fopen("LOG00.BIN", "rb+");
    fseek(f_log, 38 * sizeof(SensorRecord), SEEK_SET);

    uint8_t garbage[2 * sizeof(SensorRecord)];
    memset(garbage, 0xFF, sizeof(garbage));
    fwrite(garbage, 1, sizeof(garbage), f_log);
    fclose(f_log);

    CircularLog log_recovered;
    log_recovered.mount();

    assert(log_recovered.currentFile() == 0);
    assert(log_recovered.writeIndex() == 38);

    std::cout << "PASSED\n";
}

void test_meta_crc_rejection()
{
    std::cout << "Running test_meta_crc_rejection... ";
    CircularLog log;
    log.eraseAll(ERASE_MAGIC);

    for (uint32_t i = 0; i < 40; ++i)
    {
        log.writeRecord(createDummyRecord(i));
    }

    // Corrupt ONE byte at offset 20 - inside total_records, far from
    // magic (bytes 0-3) and version (bytes 4-7). Magic check passes,
    // CRC check must fail.
    FILE *f_meta = fopen("META.BIN", "rb+");
    assert(f_meta != nullptr);
    fseek(f_meta, 20, SEEK_SET);
    int c = fgetc(f_meta);
    fseek(f_meta, 20, SEEK_SET);
    fputc(c ^ 0xFF, f_meta);
    fclose(f_meta);

    // All 40 records are still intact, so recovery must land the head
    // one past the newest valid record: file 0, index 40.
    CircularLog log_recovered;
    log_recovered.mount();

    assert(log_recovered.currentFile() == 0);
    assert(log_recovered.writeIndex() == 40);

    std::cout << "PASSED\n";
}

// [Claude] added: A6.1 watchdog-starvation audit. flushMeta()/snapshot() used to block
// forever (xSemaphoreTake(..., portMAX_DELAY)) if another task held the CircularLog mutex,
// which could starve runSystemTask's IWDG kick past the ~4s hardware timeout. Verifies every
// caller now fails fast (IoError, or a safe zeroed/not-mounted snapshot) instead of hanging
// when the lock can't be acquired -- see semphr.h's g_forceSemaphoreTakeFail.
void test_lock_timeout_fails_fast()
{
    std::cout << "Running test_lock_timeout_fails_fast... ";

    CircularLog log;
    log.eraseAll(ERASE_MAGIC); // clean, mounted start
    log.writeRecord(createDummyRecord(0));

    g_forceSemaphoreTakeFail = true;

    assert(log.writeRecord(createDummyRecord(1)) == StorageStatus::IoError);
    assert(log.flushMeta() == StorageStatus::IoError);

    StatusSnapshot snap = log.snapshot();
    assert(snap.mounted == false);
    assert(snap.totalRecords == 0);

    g_forceSemaphoreTakeFail = false;

    // lock works again afterwards -- this isn't a permanent trip
    assert(log.writeRecord(createDummyRecord(2)) == StorageStatus::Ok);
    assert(log.flushMeta() == StorageStatus::Ok);

    std::cout << "PASSED\n";
}

int main()
{
    std::cout << "--- Starting Storage Host Tests ---\n";
    test_fill_single_file();
    test_full_wrap_around();
    test_overwrite_after_wrap();
    test_replay_10_of_15();
    test_replay_400_of_300();
    test_replay_crossing_boundary();
    test_erase_bad_magic();
    test_recovery_and_meta_corruption();
    test_meta_crc_rejection();
    test_lock_timeout_fails_fast();
    std::cout << "--- All Tests Passed ---\n";
    return 0;
}
