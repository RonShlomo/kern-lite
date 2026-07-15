# KERN-LITE Design Note

## 1. LogMeta byte layout

36 bytes, packed, little-endian (`firmware/storage/circular_log.hpp`):

| Offset | Size | Field | Meaning |
|---:|---:|---|---|
| 0 | 4 | magic | `0x4C4F4700` |
| 4 | 4 | version | `1` |
| 8 | 1 | file_count | `4` |
| 9 | 2 | records_per_file | `256` |
| 11 | 1 | current_file | active file index |
| 12 | 2 | write_index | next slot in current_file |
| 14 | 4 | wrap_count | completed ring wraps |
| 18 | 4 | total_records | cumulative writes since erase |
| 22 | 10 | reserved | padding |
| 32 | 4 | crc32 | over bytes 0-31 |

## 2. Recovery algorithm

On mount, `readMeta()` checks magic/version/CRC. Whether or not it's valid, `recoverPosition()`
scans every slot in all four files, validates each record's own CRC-32, and finds the newest
valid one via modulo-aware sequence comparison. The next slot after it becomes the recovered
write head. This always runs, even with a valid checkpoint, because metadata is flushed only
every 16 records and on STOP — the checkpoint can trail the true head. Recovery never rewrites
or erases a record.

## 3. Robustness experiment: record loss under load

**Test**: `test_commands.py` — 30s live recording at 10Hz, REPLAY 60, ERASE, on hardware.

**Observed**: live stream was perfect (no CRC errors, no gaps), but only 240/288 records
(~17%) reached storage. REPLAY showed scattered gaps and a trailing-record mismatch.

**Root cause (two compounding issues)**:
1. `SensorBus` is correctly a single-slot mailbox (per spec), but the Sensor and Storage
   tasks each ran independent `vTaskDelay(100ms)` loops. Storage's `f_sync()` latency isn't
   fixed, so its real period crept past Sensor's, eventually causing a publish to be
   overwritten before Storage read it. **Fix**: both tasks switched to `vTaskDelayUntil`
   (fixed schedule, no cumulative drift) — cut loss to ~5%.
2. Residual loss was periodic every ~20 records: `Dht11::read()` blocks ~18-25ms (mandatory
   18ms start pulse) and ran *before* `bus.publish()` every 20th tick, delaying that one
   publish enough for Storage's now-fixed schedule to skip past it. **Fix**: DHT11 poll moved
   to run after publish/transmit; any fault it detects now applies to the next record instead.

**Result**: 23/23 checks pass; live/stored difference down to ~2%, no longer causing gaps.

**Known limitation**: that residual ~2% difference persists and hasn't been isolated further —
likely genuine SD write-latency variance rather than a scheduling artifact.

## 4. Deviations from handbook reference code

None change any protocol frame, record layout, metadata format, or public interface.

- Sensor/Storage tasks use `vTaskDelayUntil` instead of the handbook's `vTaskDelay` (already
  the pattern `runSystemTask()` used for watchdog timing).
- DHT11 poll runs after publish/transmit instead of before.
- `_FS_LOCK` is set to `2` in `ffconf.h`, capping FatFs to 2 simultaneously-open files.
  `CircularLog` (same `m_files`/`m_filesOpen` members as the handbook's reference) now opens
  the one log file it needs per operation and closes it immediately, instead of holding all
  four open for the session.
