# kern-lite

readme

## Why SensorBus is a queue, not a mutex-guarded slot

The Sensor task published one record per 100ms into a single overwritable slot; the Storage task
read "latest" every 100ms. If a write (open+write+f_sync+close, needed for `_FS_LOCK=2`) ever ran
past 100ms, the next publish overwrote the record Storage hadn't read yet — a silent, undetected
loss. `phase5_integration_report_*.txt` shows this happening on every run (2-18% of records).

`SensorBus` (`firmware/recorder/sensor_bus.hpp`) is now a static 8-deep queue: `publish()`
enqueues, `latest()` dequeues, and the Storage task drains it fully each tick. A slow write now
just delays draining instead of losing data. Same public API; `_FS_LOCK=2` untouched.
