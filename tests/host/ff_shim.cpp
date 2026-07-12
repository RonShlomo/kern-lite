// tests/host/ff_shim.cpp
// Implements the prototypes in tests/host/ff.h by forwarding to standard C stdio.
// Host-test build only. Never add this file to the firmware build.

#include "ff.h"
#include <cstdio>

// FatFs paths carry a drive prefix like "0:LOG00.BIN". That's meaningless
// (and invalid on Windows) for stdio, so strip it before touching the disk.
static const char *stripDrive(const char *path)
{
    if (path && path[0] != '\0' && path[1] == ':')
    {
        return path + 2;
    }
    return path;
}

extern "C"
{

    FRESULT f_mount(FATFS *fs, const char *path, uint8_t opt)
    {
        // No card to mount on a PC; the "filesystem" is just the working directory.
        (void)fs;
        (void)path;
        (void)opt;
        return FR_OK;
    }

    FRESULT f_open(FIL *fp, const char *path, uint8_t mode)
    {
        if (fp == nullptr)
            return FR_DISK_ERR;
        path = stripDrive(path);

        // FatFs FA_OPEN_ALWAYS means: open if it exists, create if it doesn't,
        // and keep existing content. stdio has no single mode for that, so:
        // try to open existing for read+write ("r+b"); if that fails, create ("w+b").
        if (mode & FA_OPEN_ALWAYS)
        {
            fp->pc_file = std::fopen(path, "r+b");
            if (fp->pc_file == nullptr)
            {
                fp->pc_file = std::fopen(path, "w+b");
            }
        }
        else if (mode & FA_CREATE_ALWAYS)
        {
            // Create or truncate to zero length.
            fp->pc_file = std::fopen(path, "w+b");
        }
        else if (mode & FA_WRITE)
        {
            // Open existing for read+write; fail if missing.
            fp->pc_file = std::fopen(path, "r+b");
        }
        else
        {
            // FA_OPEN_EXISTING / FA_READ: read-only, fail if missing.
            fp->pc_file = std::fopen(path, "rb");
        }

        return (fp->pc_file != nullptr) ? FR_OK : FR_DISK_ERR;
    }

    FRESULT f_close(FIL *fp)
    {
        if (fp == nullptr || fp->pc_file == nullptr)
            return FR_DISK_ERR;
        int rc = std::fclose(fp->pc_file);
        fp->pc_file = nullptr;
        return (rc == 0) ? FR_OK : FR_DISK_ERR;
    }

    FRESULT f_read(FIL *fp, void *buff, UINT btr, UINT *br)
    {
        if (fp == nullptr || fp->pc_file == nullptr)
            return FR_DISK_ERR;
        size_t n = std::fread(buff, 1, btr, fp->pc_file);
        if (br)
            *br = static_cast<UINT>(n);
        // Note: FatFs reports a short read via *br < btr, not via an error code.
        return FR_OK;
    }

    FRESULT f_write(FIL *fp, const void *buff, UINT btw, UINT *bw)
    {
        if (fp == nullptr || fp->pc_file == nullptr)
            return FR_DISK_ERR;
        size_t n = std::fwrite(buff, 1, btw, fp->pc_file);
        if (bw)
            *bw = static_cast<UINT>(n);
        return (n == btw) ? FR_OK : FR_DISK_ERR;
    }

    FRESULT f_lseek(FIL *fp, uint32_t ofs)
    {
        if (fp == nullptr || fp->pc_file == nullptr)
            return FR_DISK_ERR;
        int rc = std::fseek(fp->pc_file, static_cast<long>(ofs), SEEK_SET);
        return (rc == 0) ? FR_OK : FR_DISK_ERR;
    }

    FRESULT f_sync(FIL *fp)
    {
        if (fp == nullptr || fp->pc_file == nullptr)
            return FR_DISK_ERR;
        int rc = std::fflush(fp->pc_file);
        return (rc == 0) ? FR_OK : FR_DISK_ERR;
    }

    FRESULT f_unlink(const char *path)
    {
        int rc = std::remove(stripDrive(path));
        return (rc == 0) ? FR_OK : FR_DISK_ERR;
    }

} // extern "C"