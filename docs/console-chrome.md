# Prop console chrome â€” px-dynamite-v1

Copied from [px-patch-v1/docs/console-chrome.md](../../px-patch-v1/docs/console-chrome.md) /
[px-wifi-v1/docs/console-chrome.md](../../px-wifi-v1/docs/console-chrome.md), then
specialized for the dynamite cabinet. Theme: **Signal Glass**.

## Pages

| Page | File | Job |
|------|------|-----|
| Live | `index.html` | Cabinet 2/3 + Quick Actions 1/3. Target Charge pane (gram sum vs target). Send Bay grey until 4-in, then green/red on the sum. |
| Config | `config.html` | Game mode (Basic I/O / Code Entry), eight named codes Aâ€“H, target gram sums (reject impossible), charge weights in g. |
| Monitor | `monitor.html` | 16-reed grid, decoded `id0..id3`, keypad matrix, Bank1/Bank2, maglock pulse. |
| Connect | `connection.html` | Wi-Fi, MQTT (default broker `.132`), mDNS/identity, OTA link |
| OTA | `update.html` | Firmware upload from Connect |
| Samples | `samples.html` | Dummy-data scenarios for UI review (not shipped later) |

Tab order: **Live, Config, Monitor, Connect**.

## Look

Same CSS tokens as px-wifi-v1 Signal Glass. Dynamite extras live at the bottom of
`styles.css` (cabinet, charge sticks, reed grid, keypad matrix).


## Responsive (required)

Must work on phone (~390px), tablet (~768px), and desktop. Shared chrome rules
live in `styles.css` and are documented in
[px-wifi-v1/docs/console-chrome.md](../../px-wifi-v1/docs/console-chrome.md):

- `max-width: 820px` — stack `.layout` **and** `.layout.live-layout`; wrap tabs
- `max-width: 520px` — phone padding / single-column metrics; prop-specific grids

Do not ship UI changes without checking those widths. Bootstrap is optional.

## Local UI iteration (no flash)

```powershell
# from px-dynamite-v1/
.\scripts\serve_webui.ps1
```

http://127.0.0.1:8094/index.html â€” demo mocks auto-on for localhost.

Scenario query: `?scenario=empty|partial|ready|door-open|send|no-pressure|pulse|io|mismatch|mqtt`
