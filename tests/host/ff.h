// tests/host/ff.h
#ifndef FF_MOCK_H
#define FF_MOCK_H

#include <cstdio>
#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

// Basic types required by FatFs
typedef int FRESULT;
#define FR_OK 0
#define FR_DISK_ERR 1
#define FA_READ 0x01
#define FA_WRITE 0x02
#define FA_OPEN_ALWAYS 0x10
#define FA_CREATE_ALWAYS 0x08
#define FA_OPEN_EXISTING 0x00

typedef unsigned int UINT;

// We map FIL directly to standard C FILE pointer
struct FIL {
    FILE* pc_file;
};

struct FATFS {
    int dummy;
};

// Function prototypes
FRESULT f_mount(FATFS* fs, const char* path, uint8_t opt);
FRESULT f_open(FIL* fp, const char* path, uint8_t mode);
FRESULT f_close(FIL* fp);
FRESULT f_read(FIL* fp, void* buff, UINT btr, UINT* br);
FRESULT f_write(FIL* fp, const void* buff, UINT btw, UINT* bw);
FRESULT f_lseek(FIL* fp, uint32_t ofs);
FRESULT f_sync(FIL* fp);
FRESULT f_unlink(const char* path);

#ifdef __cplusplus
}
#endif

#endif // FF_MOCK_H
