# px-dynamite-v1

ESP32 dynamite-cabinet firmware — Paradox TFD control room.

## Status (2026-09-04)

Firmware **0.02**. Crafty Fox wiring is **reed SPI** (2× MCP23S17, keypad,
maglock GPIO 23) — **not** the RFID `dynamite32` tree.

The ESP is I/O only: report `{keypress}` / `{allConnected, id0..id3,
doorOpen}`, pulse maglock. Keypad codes and win stay in `control.js`.

## License

Dual-licensed:

- **AGPL-3.0** for open source use — see [LICENSE](LICENSE).
- **Commercial license required** for proprietary or revenue-generating use that does not comply with AGPL-3.0 — see [COMMERCIAL.md](COMMERCIAL.md).

Copyright © 2026 Mark Stevens.

## Local UI preview

```powershell
.\scripts\serve_webui.ps1
```

- http://127.0.0.1:8094/index.html — Live (cabinet + four slots)
- http://127.0.0.1:8094/config.html — Config (scan + maglock pulse cap)
- http://127.0.0.1:8094/monitor.html — Monitor (16 reeds + keypad)
- http://127.0.0.1:8094/connection.html — Connect (broker `.132`)
- http://127.0.0.1:8094/samples.html — dummy-data scenarios

`localhost` enables demo mocks automatically. `?demo=1` / `?demo=0` override.
`?scenario=empty|partial|ready|door-open|send|no-pressure|pulse`

Port **8094** so it does not collide with px-wifi-v1 (8090), px-fuse-v1 (8091),
px-valve-v1 (8092), or px-patch-v1 (8093).

## Firmware

Build: `idf.py set-target esp32` then `idf.py build` (ESP-IDF 6.0.x).
Artifact: `build/px-dynamite-v1.bin`.

OTA:

```powershell
.\scripts\ota_upload.ps1 -HostAddress <ip>          # new /api/ota/upload
.\scripts\ota_upload.ps1 -HostAddress <ip> -Legacy  # old POST /ota
```

Default STA `Paradox-TFD-1` / `tfd1-jr6t`, broker `192.168.8.132`.
Pin ESP-IDF 6.0.x, `EXTRA_COMPONENT_DIRS` → `../px-components`. See
[docs/console-chrome.md](docs/console-chrome.md) and
[rooms/tfd/docs/ESP32-DYNAMITE-PLAN.md](../../rooms/tfd/docs/ESP32-DYNAMITE-PLAN.md).
