# px-dynamite-v1 — AI Instructions

TFD control-room dynamite cabinet firmware for Paradox escape rooms.

## Status

UI prototype **0.01**. No firmware on the trailer yet. Crafty Fox is **reed
SPI / no RFID**. I/O only: reeds, pressure, keypad, maglock pulse, door.
Win (all charges present + `*`) stays in `control.js`.

## Firmware (when implemented)

- Target: classic **esp32**, IDF **6.0.x**, flash **4MB**, version from `version.txt`.
- Default STA: `Paradox-TFD-1`; broker **`192.168.8.132`**.
- SoftAP SSID form: `Paradox-PXDynamiteV1-XXXX`.
- **I/O only.** Do not put safe codes or `success()` on the ESP.
- **Do not trailer-flash until a human asks.** Bring the unit onto `.132`
  Wi-Fi first.

Local UI preview without flash:

```powershell
.\scripts\serve_webui.ps1
```

→ http://127.0.0.1:8094/index.html (wifi=8090, fuse=8091, valve=8092, patch=8093).

Scenario mocks: http://127.0.0.1:8094/samples.html

Admin UI chrome: [docs/console-chrome.md](docs/console-chrome.md). Plan:
[rooms/tfd/docs/ESP32-DYNAMITE-PLAN.md](../../../rooms/tfd/docs/ESP32-DYNAMITE-PLAN.md).

Pin source is [tfd-old/Archive/dynamite/dynamite/dynamite.ino](../../../rooms/tfd-old/Archive/dynamite/dynamite/dynamite.ino), **not** RFID `archive/dynamite32`.

## MQTT (legacy CF)

| Topic | Payload |
|-------|---------|
| `/Paradox/ParadoxDynamiteProp/command` | `{magLock:0\|1}`, `reportState` |
| `/Paradox/ParadoxDynamiteProp/state` | `{keypress}`, `{id0..id3, allConnected, doorOpen}` |
| `/Paradox/Props` | heartbeat id `Dynamite32Prop` |
| `/Paradox/ParadoxDynamiteProp/Props` | same, for the GM lamp |

## Other conventions

- Do not edit `props/esp32/archive/dynamite32` or `tfd-old` runtime (except the
  planned `allConnected` gate in `control.js` later).
- Version bump default `+0.01`.
- Path-relative assets + `lib_http_proxy` when serving embedded UI.

## Suite standards

Public suite brief + contracts: [../../../apps/PxH/docs/standards/](../../../apps/PxH/docs/standards/).
