# Changelog

All notable changes to px-dynamite-v1 are documented here.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).
Version numbers correspond to the contents of `version.txt`.

## [Unreleased]

## [0.09] - 2026-09-08

### Fixed

- MCP init and the I/O loop now start **after** SoftAP/STA, so a wedged SPI
  bus cannot keep the console offline (valve 0.09 lesson).
- SPI transfers use `spi_device_queue_trans` + `get_trans_result` with an
  80 ms bound. IDF 6 rejects `polling_start` timeouts and `polling_transmit`
  cannot time out.

## [0.08] - 2026-09-07

### Fixed

- Missing MCP23S17 expanders no longer fail init or starve the scan loop.
  Wi-Fi / SoftAP stay up; a red banner reports `hwFault` until both chips
  answer a write/readback.

## [0.07] - 2026-09-07

### Added

- GM overrides: `solveCode` (correct code path; keypresses + audio event only
  the first time after reset), `openCabinet` (maglock pulse), `sendCharges`
  (`*` only after code solved). Live buttons + `reset` clears the latch.

## [0.06] - 2026-09-04

### Fixed

- Live Target code dropdown was empty on the ESP: `/api/state` omitted the
  configured codes, and the UI did not fall back to `/api/config`.

## [0.05] - 2026-09-04

### Added

- Live keypad `{keypress}` inject and Monitor held-key highlight, shipped
  to the live replacement at `.57` (`dynamite.local`).

## [0.04] - 2026-09-04

### Added

- Live keypad clicks inject `{keypress}` the same way as the physical pad
  (entry window, MQTT, last key).
- Monitor lights the key that is currently held, including UI-injected
  presses (held ~2 s so the poll can see them). The keypad no longer
  outlines the scan row — that ring flickered too fast to read.

## [0.03] - 2026-09-04

### Fixed

- Boot loop when the reed MCP23S17 is absent: 8 KB config buffers no longer
  sit on the 8 KB `main` stack. Main stack raised to 16 KB.
- `dyn_loop` now delays at least one RTOS tick so a missing expander cannot
  starve IDLE0 / the task watchdog.

## [0.02] - 2026-09-04

### Added

- ESP-IDF firmware for classic esp32: dual MCP23S17 reed/keypad scan, maglock
  pulse on GPIO 23 with a hard 400 ms cap, door on GPIO 33.
- MQTT `/Paradox/ParadoxDynamiteProp/{command,state}` plus dual heartbeat
  (`/Paradox/Props` and `.../Props`) as `Dynamite32Prop`.
- HTTP OTA (`POST /api/ota/upload`) and Signal Glass console served from flash.

## [0.01] - 2026-09-04

### Added

- Signal Glass prop console (Live / Config / Monitor / Connect / OTA) copied from
  px-patch-v1 / px-wifi-v1 and specialized for the reed-SPI dynamite cabinet.
- Live: Send Bay + Storage Bay (bullet charges), Code Entry rolling 7-seg window,
  keypad, cabinet door drawing, Open Cabinet maglock pulse.
- Game modes Basic I/O and Code Entry; eight named codes A–H on Config.
  MQTT `targetCode` can override the current game without writing A–H.
- Dummy-data scenarios on port **8094**. No firmware yet. No RFID.
- Weights in **grams** (0.65 kg → 650 g). Target Charge is a gram sum; Send Bay
  stays grey until four charges are in, then green/red on the sum. Config rejects
  target sums with no 4-charge solution.
