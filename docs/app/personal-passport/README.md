<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Player Pass Personal Card

Player Pass is a derivative FoloToy AI Passport application: a pocket electronic
business card that shows a pixel avatar, profile text, game collection, contact
codes, and a configurable NFC payload. All personal data lives on the device.
There is no cloud account, login, or OTA update. The management console provides a
manual JSON export/import; there is no automatic backup.

The firmware targets the documented ESP32-C3 board (8 MB Flash, no PSRAM,
240 x 320 ST7789P3, three ADC-ladder buttons). It keeps board access in
`components/bsp` and places every page, task, state machine, and web asset in
`main`.

## Device experience

### Controls

| Input | Action |
| --- | --- |
| UP / DOWN short press | Move the selection, clamped at both ends |
| OK short press | Enter a page, open a QR code, or show a detail panel |
| OK long press | Go back one level / close an overlay |
| OK double press on Home | Enter the management AP mode |

The selected row always has a mint border and highlighted text. Every page has a
footer with its available actions.

### Pages

1. **Home** - 92 x 92 avatar frame, name, one-line introduction, three platform
   rows (Steam / Switch / PSN) with their IDs and QR availability, and three
   navigation rows (Games, Contact, NFC). Pressing OK on a platform shows its
   uploaded QR code; pressing OK on a navigation row opens that page.
2. **Games** - one continuous list with two sections, "Favourite games" and
   "Playing". Each row shows one portrait cover, the title, and a short note.
   The list is rendered as a moving five-entry window, so the object count stays
   small even if the stored list is long. OK opens the game detail page.
3. **Game detail** - 72 x 96 cover, title, group badge, Xiaoheihe link status,
   and introduction. If a link is configured, OK renders a QR code on the
   device for the phone to scan.
4. **Contact** - Weibo, WeChat name, WeChat ID, WeChat QR, and WeChat payment
   QR. Text values open a large read-only panel; QR entries open a 180 x 180
   white QR page. QR codes are never all shown at once.
5. **NFC** - the configured label, type, and payload. The NTAG213 is a passive
   tag with no MCU interface; the firmware displays the payload and can render a
   QR copy, but it cannot write the tag. Write it with an NFC-capable phone.
6. **Management mode** - a fixed screen with the WPA2 SSID, fixed password, a
   scannable QR code for the management URL, client/lease state, storage bar,
   and manual exit. Normal navigation is suspended until long-pressing OK.

The battery percentage is shown in the top-right corner of every page. A reading
of -1 renders `--` instead of a number.

## Visual system

| Role | Colour |
| --- | --- |
| Background | `#0B1220` deep blue-gray |
| Panel | `#141E30` / `#1B2B3F` |
| Border | `#2A3A55` low-saturation blue-gray |
| Accent | `#5EEAD4` mint |
| Text | `#E6EEF8` |
| Muted | `#8CA3C0` |
| Warning / error | `#FBBF24` / `#F87171` |

The device uses Fusion Pixel 12 px and 24 px bitmap fonts, hard 1-2 px borders,
no rounded cards, and a shared 14 x 14 alpha-mask icon set. The management
console mirrors the palette, hard borders, monospace numbers, and an empty
background grid. It is a single offline page with no external fonts, scripts, or
network requests.

## Architecture

```text
main/
  main.c                 BSP bring-up, then app_core_start()
  app/                   lifecycle, data loading, input worker, mode switching
  data/                  profile, games, NFC models and JSON validation
  storage/               portable POSIX file layer, LittleFS mount, thresholds
  image/                 PPIM header, image loading, on-device QR rendering
  session/               lease state machine and optional idle timer
  wifi_manager/          WPA2 SoftAP lifecycle
  http_server/           management API and embedded web assets
  ui/                    redesigned device screens, theme, icons
  font/                  font declarations and glyph coverage checks
  web_src/               management console sources
  web/web_assets.c       generated gzip asset arrays
  third_party/           cJSON 1.7.18 and Nayuki QR generator (MIT)
```

LVGL is only touched from the input worker while holding `bsp_lvgl_lock()`.
Button callbacks only enqueue events. Image reads and the HTTP server run in
their own tasks. Audio is intentionally not initialized because the product does
not play sound; this keeps internal RAM and power use lower.

## Storage and partition layout

The device uses one factory application partition (no OTA) and one LittleFS data
partition. NVS stores only Wi-Fi calibration data.

| Partition | Type | Offset | Size | Purpose |
| --- | --- | ---: | ---: | --- |
| `nvs` | data/nvs | `0x9000` | `0x6000` | Wi-Fi calibration |
| `phy_init` | data/phy | `0xF000` | `0x1000` | PHY data |
| `factory` | app/factory | `0x10000` | `0x300000` | Single application image |
| `storage` | data/littlefs | `0x310000` | `0x4F0000` | JSON records and images |

The compressed application image in the delivery build is **2,120,592 bytes**
(`0x205B90`). It fits the 3 MiB factory partition with 1,025,136 bytes (33%)
free, which leaves room for font regeneration. The data partition is
`0x4F0000` bytes (about 4.94 MiB). A blank LittleFS partition is formatted on
first boot; a non-blank partition that fails to mount is reported as corrupt
and is never formatted automatically.

## Build and validation

Activate ESP-IDF 5.5.3, then run:

```bash
./tools/validate.sh --static
./tools/validate.sh --firmware
./tools/validate.sh
```

The firmware gate builds in an isolated directory, verifies the partition table
and merged image, and writes `build/FoloToy-AI-Passport-full.bin`. The host gate
runs the portable tests plus `tools/check_fonts.py`, `tools/pack_web.py --check`,
and `tools/gen_icons.py --check`.

## Flashing and stored data

Flash the merged image from offset `0x0` for a blank device or an intentional
complete refresh:

```bash
python -m esptool --chip esp32c3 -p <port> -b 460800 \
  write-flash 0x0 build/FoloToy-AI-Passport-full.bin
```

A merged image pads the gaps between images and resets NVS (Wi-Fi calibration). In the measured build the merged image ends before the
storage partition, but do not rely on that for a future build or an intentional
complete refresh; a larger image or a different layout can reach the data area.
The product provides a manual JSON export from the management console. Export
before a complete refresh if data must be kept. For segmented development flashes,
use a compatible partition layout and do not erase the storage partition.

There is no OTA path, no dual application slot, and no automatic rollback.

## Fonts

The device fonts are generated from Fusion Pixel 12 px proportional (SIL OFL
1.1, release 2026.10.25) at 12 px and 24 px, 1 bpp, with line heights 16 and 28.
The measured Flash cost is 721,318 bytes (187,267 + 505,351 + 28,700 for the
12 px font, 24 px font, and coverage table).
The inventory contains 7,393 code points: printable ASCII, GB2312 symbols and
level-1/level-2 characters present in the source font, and every non-ASCII
character in `main/`. The source
TTF is not committed. See [`assets/README.md`](../../../assets/README.md#device-font-subset-player-pass)
for the source URL, hash, converter version, and regeneration command.

Dynamic text outside the inventory is rejected by the management API and replaced
with `?` in the device UI, with a `U+XXXX` log line. Regenerate both fonts and
rebuild the firmware when the supported set must grow. The firmware does not
claim support for arbitrary Unicode.

## Management console

The management console is embedded in the firmware as deterministic gzip arrays
and served at `http://192.168.4.1`. It works on phone and desktop browsers in
portrait and landscape. The AP is WPA2 protected; the console itself has no
login password. The AP uses a fixed product password defined in the firmware. The
first browser to claim the lease edits; other browsers can
read status and see that the device is occupied. Text changes and image uploads
are written to LittleFS with temporary files and atomic replacement.

The backup tab exports profile, games, NFC, and every image as one JSON file;
import overwrites the current data and requires the editor lease. See
[data-and-api.md](data-and-api.md) for the storage format, backup format, and
every API endpoint.

## Acceptance checklist

Automated:

- `./tools/validate.sh --static` passes.
- `./tools/validate.sh --firmware` produces and verifies the merged image.
- Host tests cover UTF-8, JSON, atomic replacement, image validation, storage
  thresholds, game grouping, lease/grace/idle state, and font coverage.

On the device (not established by a build):

- Boots to the redesigned Home; avatar, name, tagline, IDs, battery render.
- All three buttons: selection clamps, OK actions, long-OK back, double-OK on
  Home enters management mode.
- Games sections and window scrolling; details; on-device QR generation.
- Contact text panels and uploaded QR/payment images.
- NFC page and QR payload.
- Management AP is visible, WPA2 protected, and the screen shows SSID/password/
  URL; repeated enter/exit cycles do not leak tasks or sockets.
- A phone opens the console; profile, games, images, contact, and NFC changes
  persist across a reboot.
- A second browser sees "occupied"; the first browser keeps the lease with
  heartbeats; grace, no automatic exit, and long-press OK exit behave as documented.
- 80% / 90% storage warnings; failed uploads preserve the previous image.
- The management screen shows the URL QR; Chinese text on every page, including a
  dynamic name and a rejected missing glyph.
- USB Serial/JTAG logs and flashing; measured free heap/largest block with Wi-Fi
  and HTTP active.

## Delivery status

Recorded for `feature/personal-passport` on 2026-11-10. A successful build is
not hardware acceptance.

| Item | Result |
| --- | --- |
| Build | PASS - ESP-IDF 5.5.3; application 2,120,592 bytes (`0x205B90`); merged image 2,186,128 bytes |
| Host tests | PASS - `./tools/validate.sh --static` and the complete `./tools/validate.sh` |
| Device tests | NOT RUN - no USB Serial/JTAG device was detected in this environment |
| Unverified | display/backlight/font rendering; all three buttons and navigation; Wi-Fi AP visibility and WPA2; phone console access, lease/grace and manual exit; image upload and persistence; manual export/import; first-boot LittleFS format; battery readings; USB logs/flashing; phone cellular data behavior |

Merged image: `build/FoloToy-AI-Passport-full.bin`

SHA-256: `bf76f44330f2913e2c29a21e4b08eb3a5095fa9cf8e49b2530610727aabe71e0`

Verified debug bundle:
`build/firmware/bf76f44330f2913e2c29a21e4b08eb3a5095fa9cf8e49b2530610727aabe71e0/`

## Management page does not respond

If the phone joined `PlayerPass-XXXX` but `http://192.168.4.1` times out:

1. Look at the device management screen. It must show "hotspot and management page
   running" and the URL QR code. A "starting" or "start failed: management page"
   status means the HTTP server did not start; long-press OK, re-enter management,
   and read the error or USB log.
2. In the phone's Wi-Fi details, confirm an address in the 192.168.4.0/24 subnet
   and gateway 192.168.4.1. If there is no address or it starts with 169.254,
   forget the network and rejoin, or reboot the device.
3. Open exactly `http://192.168.4.1/` - not HTTPS, not `.local`, and no custom
   port. Try a private window or another browser. Disable VPNs and Private Relay;
   on Android, disable automatic switching to mobile data for this AP and choose
   "stay connected" when warned that the network has no internet.
4. From a laptop joined to the same AP:

   ```bash
   ping -c 4 192.168.4.1
   ```

   ```bash
   curl -v --max-time 5 http://192.168.4.1/api/status
   ```

   If ping fails, the issue is association/DHCP. If ping succeeds but curl fails,
   the HTTP server is not accepting connections; re-enter management and inspect
   the device status/log.
5. The AP accepts at most three clients; disconnect unused devices.
6. If the device still runs an older build, flash the current full image. The new
   management screen shows the URL QR and a detailed start-failure message.
7. If the AP stays up but HTTP never starts, capture USB Serial/JTAG logs with a
   data cable and a port that enumerates as `/dev/cu.usbmodem*`.

The AP has no internet by design; the local page should still load. Do not use a
captive-portal probe URL or assume the phone will open the page automatically.

## Limitations and unverified behavior

- The NTAG213 payload cannot be written by the firmware; it is configuration for
  an external NFC writer.
- Phone behaviour when the AP has no internet varies by OS. iOS usually keeps
  cellular data available; some Android devices route traffic through the AP and
  lose internet until it is disconnected. This is not verified for every model.
- The web console never calls Xiaoheihe or any external service. Game links are
  rendered as QR codes for the phone; cross-origin and anti-bot behaviour are
  outside the firmware.
- The AP password is a fixed product password in the firmware. Anyone who knows it
  can join the AP; the console has no login. Rotate it by rebuilding the firmware.
- Backup is manual JSON export/import only. There is no automatic backup, ZIP,
  history, restore point, or rollback. A flash or filesystem failure can lose data.
- Device rendering, button feel, Wi-Fi range, battery readings, and USB
  behaviour require physical acceptance.
