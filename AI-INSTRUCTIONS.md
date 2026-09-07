# px-dynamite-v1 — AI Instructions

TFD control-room dynamite cabinet firmware for Paradox escape rooms.

## Status

Firmware **0.07** on the live cabinet replacement at **`192.168.8.57`**
(`MAC 24:0a:c4:c1:25:00`, mDNS **`dynamite.local`**). Reeds / pressure /
keypad / maglock pulse / door. Safe-code **matching for win** stays in
`control.js`; GM overrides on the prop: `solveCode`, `openCabinet`,
`sendCharges` (Live buttons). Live keypad clicks POST `{keypress}` so they
match a physical press.

The old Arduino ParadoxProp unit (**`.53`**, `24:0a:c4:1d:35:cc`) is retired /
offline — do not treat `.57` as a spare. `.57` is the production board on
`Paradox-TFD-1` with broker **`.132`**.

## Firmware

- Target: classic **esp32**, IDF **6.0.x**, flash **4MB**, version from `version.txt`.
- Artifact: `build/px-dynamite-v1.bin` (OTA payload).
- Default STA: `Paradox-TFD-1` / `tfd1-jr6t`; broker **`192.168.8.132`**.
- OTA: browser `update.html`, or
  `.\scripts\ota_upload.ps1 -HostAddress <ip>` (new `/api/ota/upload`),
  `.\scripts\ota_upload.ps1 -HostAddress <ip> -Legacy` (old ParadoxProp `POST /ota`).
- SoftAP SSID form: `Paradox-PXDynamiteV1-XXXX`.
- Default mDNS hostname: **`dynamite.local`** (`networkName` in Connect / NVS).
- Maglock GPIO 23 is a **short self-timed pulse** (default 250 ms, hard cap 400 ms).
- GM: `solveCode` (keypresses + audio event once/reset), `openCabinet`,
  `sendCharges` (`*` only after code solved). Does not hold the maglock coil.

Local UI preview without flash:

```powershell
.\scripts\serve_webui.ps1
```

→ http://127.0.0.1:8094/index.html (wifi=8090, fuse=8091, valve=8092, patch=8093).

Scenario mocks: http://127.0.0.1:8094/samples.html

Admin UI chrome: [docs/console-chrome.md](docs/console-chrome.md) (must work on
phone / tablet / desktop — see Responsive section). Plan:
[rooms/tfd/docs/ESP32-DYNAMITE-PLAN.md](../../../rooms/tfd/docs/ESP32-DYNAMITE-PLAN.md).

Pin source is [tfd-old/Archive/dynamite/dynamite/dynamite.ino](../../../rooms/tfd-old/Archive/dynamite/dynamite/dynamite.ino), **not** RFID `archive/dynamite32`.

## MQTT (legacy CF)

| Topic | Payload |
|-------|---------|
| `/Paradox/ParadoxDynamiteProp/command` | `{magLock:0\|1}`, `{keypress}`, `reportState` |
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
