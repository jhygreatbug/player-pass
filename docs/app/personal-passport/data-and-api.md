<p align="right">
  <a href="data-and-api.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Player Pass Data and API

This document defines the on-device storage format and every endpoint served by
the management AP. The web console is a client of this API; no other service is
required.

## LittleFS layout

```text
/lfs/
  data/
    profile.json        profile text and generic contact list
    games.json          game list, normalized to favourites then playing
  img/
    avatar.bin          88 x 88
    games/<game-id>.bin 216 x 101
```

Image presence is derived from the filesystem. A JSON record never stores a
path or a boolean that could become stale. The web API adds `images` / `cover`
flags to responses for convenience.

## Image container (PPIM)

All images use a 20-byte header followed by raw RGB565 little-endian pixels.
There is no JPEG/PNG decoder on the device; the browser produces exactly the
size the UI displays.

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | magic `50 50 49 4D` (`PPIM`) |
| 4 | 1 | version, `1` |
| 5 | 1 | format, `1` = RGB565 little-endian |
| 6 | 1 | flags, `0` |
| 7 | 1 | reserved, `0` |
| 8 | 2 | width, little-endian u16 |
| 10 | 2 | height, little-endian u16 |
| 12 | 4 | pixel byte length, little-endian u32 (`width * height * 2`) |
| 16 | 4 | CRC32 (IEEE) of the pixel bytes |
| 20 | N | pixel bytes |

| Slot kind | Dimensions | Complete file bytes |
| --- | --- | ---: |
| Avatar | 88 x 88 | 15,508 |
| Game cover | 216 x 101 | 43,652 |

The upload endpoint requires the exact byte length for the slot, validates the
header and dimensions, streams to `<name>.tmp`, computes the CRC, and only then
swaps it into place. A failed upload preserves the previous file.

### Atomic replacement and recovery

`pp_storage_write_atomic()` writes `<final>.tmp`, fsyncs it, renames the current
`<final>` to `<final>.old`, renames the temporary file to `<final>`, and removes
the old copy. At boot, leftover `.tmp` files are removed; a leftover `.old` file
is restored when the final file is missing and removed when the final file is
present. This allows a power loss during an update without keeping user-visible
history versions.

## JSON schemas

### profile.json

```json
{
  "version": 2,
  "name": "Player One",
  "tagline": "Plays a little every day.",
  "platforms": {
    "steam": { "id": "76561198000000000" },
    "switch": { "id": "SW-0000-0000-0000" },
    "playstation": { "id": "player_one" }
  },
  "contacts": [
    { "platform": "Weibo", "account": "@player_one", "link": "https://weibo.com/u/0" },
    { "platform": "WeChat", "account": "player_one", "link": "" }
  ]
}
```

Contacts are a generic list: each entry is an arbitrary platform name, the
account on that platform, and an optional link. The device generates a QR code
from the link; an entry without a link shows no QR code. Limits are code points:
name 24, tagline 60 (newlines allowed), each platform ID 40, up to 8 contacts,
each platform 16, account 40. Links must start with `http://` or `https://`,
contain no spaces or control bytes, and fit in 480 bytes.

Version 1 files stored fixed `contact.weibo` / `contact.wechat` objects. They are
converted on load into generic contacts, and the next save writes version 2.
Uploaded QR images from older builds are no longer read or shown.

`platforms.steam.id` is a display field. The optional recent/most-played lists
in the console accept a numeric Steam friend code (1-10 digits) or a 17-digit
SteamID64; the game-data service converts friend codes itself.

### games.json

```json
{
  "version": 1,
  "games": [
    {
      "id": "g-a1b2c3",
      "title": "A Game",
      "group": "favorite",
      "note": "Short note",
      "link": "https://example.com/game"
    }
  ]
}
```

`id` is 4-24 characters of lowercase letters, digits, and `-`. Title is
required (40 code points), note allows newlines (120), link is optional (480
bytes). `group` is `favorite` or `playing`. The array is normalized on load and
save: all favourites first, then playing, preserving relative order inside each
group. The API accepts up to 200 entries as an input/resource guard; the product
expectation is about ten.

## HTTP API

Base URL: `http://192.168.4.1`. All responses are `application/json` except
images and the static console. Mutating endpoints require the header
`X-PP-Token: <token>` returned by the claim endpoint. JSON request bodies must
carry `Content-Length`; the server rejects chunked or missing lengths.

| Method | Path | Auth | Purpose |
| --- | --- | --- | --- |
| GET | `/` | no | Management console (gzip HTML) |
| GET | `/style.css`, `/app.js` | no | Console assets (gzip) |
| GET | `/api/status?client=<id>` | optional token | Device, Wi-Fi, session, storage |
| POST | `/api/session/claim` | no | Claim the single editor lease |
| POST | `/api/session/heartbeat` | token | Refresh the edit lease |
| POST | `/api/session/release` | token | Release the lease immediately |
| GET | `/api/profile` | no | Profile plus image flags |
| PUT | `/api/profile` | token | Validate, write, and return profile |
| GET | `/api/games` | no | Games plus cover flags |
| PUT | `/api/games` | token | Replace the list and prune orphan covers |
| GET | `/api/images/<slot>` | no | Stream a PPIM file |
| PUT | `/api/images/<slot>` | token | Stream-upload a PPIM file |
| DELETE | `/api/images/<slot>` | token | Remove an image |
| POST | `/api/validate-text` | no | Check font coverage for up to 8 strings |

Slots: `avatar` and `games/<game-id>`. Contact QR codes are generated on the
device from the contact link and are not uploadable images.

### Console save model

The console loads each document once when the page opens and treats the
browser's state as authoritative while it stays open. Reconnecting after a
Wi-Fi drop re-claims the lease but never reloads documents that already loaded,
so unsaved edits survive. One **Save changes** action validates the changed text
first, then writes `profile.json` and `games.json` in that order,
uploads pending images, and applies pending image deletions. Only sections with
local changes are written. Each part is independent: a failed part keeps its
local state and is retried by the next save. The API stays per-document; there
is no combined write endpoint.

### Claim

Request:

```json
{ "client_id": "uuid", "client_name": "iPhone browser" }
```

Success (200):

```json
{
  "ok": true,
  "token": "0123456789abcdef",
  "lease_seconds": 30,
  "grace_seconds": 120,
  "idle_seconds": 0
}
```

Occupied (409):

```json
{
  "error": "occupied",
  "message": "The device is being edited by another browser",
  "client_name": "iPhone browser",
  "grace_remaining_s": 0,
  "reserved_for_me": false
}
```

A browser that loses its lease and reconnects within the grace period can claim
again with the same `client_id`. The same browser reloading while it still owns
the lease also receives a fresh token; the old token becomes invalid.

### Lease rules

| Parameter | Value |
| --- | ---: |
| Lease timeout without heartbeat | 30 s |
| Reconnect grace after lease expiry | 120 s |
| Management idle timeout | Disabled (0); long-press OK exits |
| Long operation pause | active uploads |

The first claim wins. Only one browser holds the token. A second browser can
read status and data, and sees `occupied` with the holder's name. After a missed
heartbeat the lease is reserved for the same client for two minutes; others are
still refused. Any claim, heartbeat, or authenticated mutation refreshes the
lease. While an upload is in progress, the lease is extended so a large transfer
cannot drop the editor. This product starts the session with a
zero idle timeout: management mode does not exit automatically; long-press OK
on the device exits immediately.

### Status

```json
{
  "firmware": "0.1.0",
  "uptime_ms": 123456,
  "session": {
    "running": true,
    "occupied": true,
    "editor": true,
    "reserved_for_me": false,
    "client_name": "iPhone browser",
    "grace_remaining_s": 0,
    "idle_remaining_s": 0
  },
  "storage": {
    "used": 1234567,
    "total": 5177344,
    "free": 3942777,
    "percent": 23,
    "level": 0,
    "warn_percent": 80,
    "critical_percent": 90
  },
  "wifi": { "ssid": "PlayerPass-A1B2", "ip": "192.168.4.1", "clients": 1 }
}
```

`storage.level` is 0 normal, 1 warning at 80%, 2 critical at 90%.

### Text validation

Request:

```json
{ "texts": ["Name", "A sentence"] }
```

Response:

```json
{
  "ok": false,
  "unsupported": [
    { "index": 1, "codepoint": "U+9F98" }
  ]
}
```

The server also performs this check on every profile and game write and
rejects the request with the first unsupported code point. The device sanitizer
replaces unsupported characters with `?` and logs `U+XXXX`.

## Image upload pipeline

1. Browser validates JPG/PNG (WebP and GIF are rejected), orients, and crops:
   avatar square, cover 460:215 (Steam header art).
2. Browser draws at 88 x 88 or 216 x 101, converts to RGB565 little-endian
   with ordered dithering, computes CRC32, and sends `application/octet-stream`
   with the exact length.
3. Server verifies slot, token, type, length, free space, then reads the header
   and streams pixels to a temporary file while computing CRC.
4. On success it swaps the file, refreshes image flags, and returns the new
   storage percentage. On any failure the temporary file is removed and the old
   image remains active.

Free space must cover the new file plus a 4 KiB reserve while the old file still
exists. This is checked before the transfer starts.

## Manual backup format

The backup tab is implemented entirely in the browser; there is no server-side
backup endpoint. It reads profile and games through the normal GET
endpoints, reads every referenced image through `GET /api/images/<slot>`, and
downloads one JSON file:

```json
{
  "format": "playerpass-backup",
  "version": 1,
  "exported_at": "2026-11-11T12:00:00.000Z",
  "profile": {
    "version": 2,
    "name": "Player One",
    "contacts": [
      { "platform": "Weibo", "account": "@player_one", "link": "https://weibo.com/u/0" }
    ]
  },
  "games": { "version": 1, "games": [] },
  "images": {
    "avatar": "<base64 PPIM bytes>",
    "games/g-a1b2c3": "<base64 PPIM bytes>"
  }
}
```

- The file is plain JSON, not a ZIP, and does not contain the AP password.
- Export is read-only and can run without the editor lease.
- Import requires the editor token. It validates the format and slot names,
  writes profile and games through their PUT endpoints, then uploads the fixed
  profile images and game covers. Slots absent from the backup are deleted, so
  the result matches the backup.
- Backups exported by older builds may still carry an `nfc` object; it is
  ignored on import.
- The browser accepts files up to 16 MB. Import stops at the first error and
  has no automatic rollback; export the current state before importing.
- Slot names are exactly those accepted by the image API. Legacy `qr_steam`,
  `qr_switch`, `qr_psn`, `qr_wechat`, and `qr_wechat_pay` slots in older
  backups are accepted but ignored on import. Game covers in any other format
  are also skipped, because the device only accepts 216 x 101 covers; the rest
  of the backup still imports.

## Error responses

| Status | Meaning |
| --- | --- |
| 400 | Invalid JSON, field, dimensions, CRC, or slot |
| 401 | Missing or expired editor token |
| 404 | Unknown endpoint or missing image |
| 409 | Lease occupied |
| 411 | Missing `Content-Length` |
| 413 | JSON body above the endpoint limit |
| 415 | Upload is not `application/octet-stream` |
| 500 | Storage or allocation failure |
| 507 | Not enough space for the upload |

Errors use `{ "error": "slug", "message": "human-readable text" }`.

## Security model

The AP uses WPA2-PSK with a fixed product password compiled into the firmware.
The console has no login by design; anyone who can join the AP can read data,
and the lease prevents simultaneous editing. The AP is not bridged to the
internet and the device itself never makes external requests. The console's
optional game search and player playtime lists are browser-side HTTPS calls to
the game-data service; they send only the search keyword or the Steam friend
code entered in the profile, and never require an account. The device does not
expose the AP password over HTTP; it is shown on the device screen. Rotate it by
rebuilding the firmware. NVS holds Wi-Fi calibration only.

## Phone network behaviour

The AP has no upstream internet connection. iOS usually keeps cellular data
available while joined to an AP without internet; some Android versions route
all traffic through the AP and block internet until the AP is disconnected.
The console works entirely offline for editing; game search, Steam candidate
lists, and automatic cover downloads additionally need internet access from the
phone. External links opened from the phone may require disconnecting the AP
first. This behaviour is documented as unverified until tested on the user's
phones.
