# Diagnostics and persistent logging

Microreader has two persistent diagnostic mechanisms:

1. `battery_log.csv` is a small, always-on boot/sleep history.
2. The rolling diagnostic log records detailed state transitions only when its
   compile-time debug switch is enabled.

ESP-IDF core dumps are a separate mechanism. A core-dump partition exists in
the partition table, but core-dump creation is currently disabled.

## Quick reference

| Data | Device path | Enabled | Retention |
|---|---|---|---|
| Boot/sleep history | `/sdcard/.microreader/battery_log.csv` | Always | Append-only |
| Current diagnostic log | `/sdcard/.microreader/diagnostics/diag.log` | `MR_DIAGNOSTIC_LOG=1` | 256 KiB |
| Older diagnostic logs | `diag.1.log` through `diag.3.log` | `MR_DIAGNOSTIC_LOG=1` | Four files, about 1 MiB total |
| ESP-IDF core dump | `coredump` flash partition | Currently disabled | Latest dump when enabled |

The diagnostic generations are ordered newest to oldest as follows:

```text
diag.log      newest/current
diag.1.log
diag.2.log
diag.3.log    oldest
```

At the 256 KiB limit, `diag.3.log` is deleted, the remaining files are shifted
up by one generation, and a new `diag.log` is started. The capacity is roughly
8,000–12,000 compact events and is intended to retain multiple days or weeks of
normal use.

## Compile-time switches

The switches are defined in `lib/microreader/DebugConfig.h`:

```cpp
MR_ETA_DEBUG       // existing ETA debug overlay switch
MR_DIAGNOSTIC_LOG  // persistent detailed log; defaults to MR_ETA_DEBUG
```

The detailed log therefore follows the existing debug switch unless explicitly
overridden:

```ini
; Disable both the ETA overlay and persistent diagnostic log.
build_flags =
    -Ilib
    -Wno-format-truncation
    -DMR_ETA_DEBUG=0

; Keep the overlay but remove the persistent log.
    -DMR_ETA_DEBUG=1
    -DMR_DIAGNOSTIC_LOG=0

; Disable the overlay but retain the persistent log.
    -DMR_ETA_DEBUG=0
    -DMR_DIAGNOSTIC_LOG=1
```

When `MR_DIAGNOSTIC_LOG=0`, log call sites expand to no-ops and the logger
implementation is excluded at compile time. There is no runtime condition, SD
write, or logger RAM allocation in that build.

`battery_log.csv` deliberately does **not** follow the debug switch. It writes
only once after a completed boot and once during a clean shutdown. Its cost is
negligible, while it remains useful for diagnosing resets on user devices.

## Diagnostic log format

The log is UTF-8 text with pipe-separated fields:

```text
session|uptime_ms|seq|tag|message
83a60f2c|4182|12|refresh|partial_begin width=528 height=792
83a60f2c|4874|13|refresh|partial_end
```

Fields:

- `session`: random hexadecimal identifier generated at each boot after the SD
  card is mounted.
- `uptime_ms`: monotonic milliseconds since boot. The reader has no reliable
  wall clock, so records do not contain calendar timestamps.
- `seq`: record sequence within the current session.
- `tag`: stable event category such as `boot`, `screen`, or `refresh`.
- `message`: event-specific values. Newlines and `|` characters are replaced
  with spaces so every record remains one parseable line.

Each event is appended with fixed-size stack buffers, then the file is closed.
Consequently, all completed records should survive a later hang or reset. A
sudden power loss can still leave the final line incomplete; readers should
ignore a malformed trailing line.

## Recorded events

The log intentionally records state boundaries rather than continuous internal
detail:

- `session`: start of a new log session.
- `boot`: SD ready, reset/wake cause, device model, font readiness, application
  readiness, and heap information.
- `battery`: boot counter, boot/sleep event, percentage, voltage, wake cause,
  and reset reason. This links a diagnostic session to `battery_log.csv`.
- `screen`: screen push, pop, restart, and completion. A `*_begin` record with
  no corresponding `*_end` narrows a hang to that transition.
- `reader`: book path, font readiness, MRB cache result, conversion state, and
  successful or failed reader startup.
- `input`: latched button input and active screen.
- `refresh`: partial/full refresh and panel-sleep begin/end boundaries.
- `epd_timeout`: E-paper BUSY pin, elapsed time, and the display phase that
  exceeded its timeout.
- `heartbeat`: every five minutes, including the active screen, free heap,
  largest free block, and idle time.
- `sleep`: shutdown start and completion of the main loop.

There is no log entry for each application-loop iteration, pixel operation,
layout item, or decompressed block.

## Using the logging interface

The logging module exposes only two functions through
`lib/microreader/DiagnosticLog.h`:

```cpp
MR_DIAG_INIT("/sdcard/.microreader");
MR_DIAG("reader", "ready chapter=%u", static_cast<unsigned>(chapter));
```

Guidelines for new call sites:

- Log immediately before and after a potentially blocking operation. Use
  matching names such as `operation_begin` and `operation_end`.
- Prefer stable tags and key/value messages so logs remain grep-friendly.
- Record identifiers, counts, states, and durations; do not log book contents.
- Do not log from an ISR.
- Do not log in tight loops or per-pixel/per-byte paths.
- Do not add separate file/rotation handling at call sites. Rotation and
  persistence belong to `DiagnosticLog`.
- Calls made before `MR_DIAG_INIT` are safely ignored.

The logger is currently initialized after the SD card is mounted. A failure
before SD initialization cannot be captured by this file and requires the live
serial boot output or a core dump.

## Accessing the logs

### Directly from the SD card

1. Shut the reader down cleanly.
2. Remove and mount the SD card on a computer.
3. Enable display of hidden files if necessary; `.microreader` is a hidden
   directory on Unix-like systems.
4. Copy these files before further testing:

```text
.microreader/battery_log.csv
.microreader/diagnostics/diag.log
.microreader/diagnostics/diag.1.log
.microreader/diagnostics/diag.2.log
.microreader/diagnostics/diag.3.log
```

Always preserve all generations: the event immediately preceding a failure can
be in `diag.1.log` if rotation happened during a later boot.

### Over USB serial

The firmware's existing `CMND T` command can download any SD-card file without
modifying it. `tools/serial_cmd.py` does not currently expose a public
`download` command, so direct SD access is the simplest supported workflow.

Tools that use the protocol send:

```text
"CMND" + "T" + path_length(u16 little-endian) + UTF-8 path
```

The device responds with:

```text
"READY\n" + file_size(u32 little-endian)
+ file data in chunks of at most 2048 bytes
+ crc32(u32 little-endian)
```

The host must send byte `0x06` after every data chunk before the device sends
the next one. Relevant remote paths are the `/sdcard/.microreader/...` paths in
the quick-reference table. The implementation is in
`platforms/esp32/serial_communication.h` (`case 'T'`); a host-side reference
implementation exists in the Calibre plugin's connection `download()` method.

When collecting logs from a responsive but malfunctioning reader, download the
files before rebooting if possible. A reboot is retained as a new session, but
continued operation can eventually rotate out the oldest generation.

## `battery_log.csv`

Columns:

```text
boot,event,pct,voltage_mv,uptime_ms,wake_cause,reset_reason
```

`BOOT` rows contain ESP-IDF wake and reset codes. `SLEEP` rows represent a
clean firmware shutdown and leave those two fields empty. A `BOOT` without a
matching `SLEEP` shows that the previous session ended unexpectedly, but does
not by itself identify the blocking function.

Common reset reasons for this project are:

| Value | ESP-IDF reason | Meaning |
|---:|---|---|
| 1 | `ESP_RST_POWERON` | Power-on event |
| 3 | `ESP_RST_SW` | Software restart |
| 4 | `ESP_RST_PANIC` | Exception or panic |
| 5 | `ESP_RST_INT_WDT` | Interrupt watchdog |
| 6 | `ESP_RST_TASK_WDT` | Task watchdog |
| 7 | `ESP_RST_WDT` | Other watchdog |
| 8 | `ESP_RST_DEEPSLEEP` | Deep-sleep wake |
| 9 | `ESP_RST_BROWNOUT` | Brownout |
| 11 | `ESP_RST_USB` | USB peripheral reset |

## Core dumps

`platforms/esp32/default_16MB.csv` reserves a 64 KiB `coredump` partition, but
the current generated SDK configuration selects
`CONFIG_ESP_COREDUMP_ENABLE_TO_NONE`.

If flash core dumps are enabled later, ESP-IDF creates one when its panic
handler runs, for example after an invalid memory access, `abort()`, a failed
assertion, stack corruption, or a watchdog configured to panic. It cannot save
a dump after immediate power loss, and a live deadlock or frozen E-paper panel
does not create one unless a watchdog converts the condition into a panic.

Before enabling core dumps, verify that the partition is large enough for the
16 KiB main-task stack plus the selected additional tasks, and document a tested
read/decode command for the exact firmware ELF. A 64 KiB partition may be too
small for an ELF-format dump of all relevant tasks.

## Tests

`test/unit/DiagnosticLogTest.cpp` verifies record formatting, sanitization,
rotation, and the four-generation size bound. Run it with the normal unit-test
suite:

```sh
cd test
cmake --build build2 --config Debug --target unit_tests
./build2/unit_tests --gtest_filter='DiagnosticLogTest.*'
```
