# Changelog

All notable changes to px-dynamite-v1 are documented here.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).
Version numbers correspond to the contents of `version.txt`.

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
