<p align="right">
  <a href="multi-wifi-provisioning.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Multi-Credential Wi-Fi and Provisioning Upgrade - Design Document

> Status: **Pending review (please read before implementation)**
> Scope: `main/yaogui_time_sync.c`, `main/portal.html`, `main/yaogui_network_policy.{c,h}`, `tests/test_yaogui_network_policy.c`
> Not changed: partition table (`partitions.csv`), NVS offset (`0x9000`, 24 KB), key mapping

## 1. Background and Goals

Today the device stores only **one** Wi-Fi credential. After a disconnect it just keeps retrying the same AP, and reopens the portal only after the retry limit. Moving between locations (home to office) forces re-provisioning and overwrites the old credential.

Goals for this upgrade:

1. **Multiple credentials**: migrate NVS from a single `ssid`/`password` pair to a credential array, keeping at most N entries (suggested N = 5).
2. **Smart best-candidate reconnect**: after a disconnect, scan nearby APs and try the entries that are both saved and currently visible, strongest signal first; open the hotspot only when every candidate fails.
3. **Richer provisioning page**: besides the explanation, the portal lists "saved networks that currently have signal" (removable, re-orderable) and "nearby new networks".
4. **Two-way live feedback**: the phone keeps polling connection progress (existing mechanism), and the device side (standby page / status bar) shows "provisioning / connected to XXX" in real time.

## 2. Current State and Hard Constraints (must follow)

### 2.1 Verified facts about the current code

- NVS namespace `yaogui_net`, keys `ssid` / `password`, single string pair (`load_credentials` L559, `save_credentials` L575).
- `esp_wifi_set_storage(WIFI_STORAGE_RAM)` + `init.nvs_enable = 0` (L816, L826): the stack never persists; the firmware fully owns NVS, so changing the storage format is unconstrained by the stack.
- Disconnect handler `handle_wifi_failure()` (L669): currently just `esp_wifi_connect()` to the same AP; it calls `open_portal()` only after `YAOGUI_WIFI_FAILURE_LIMIT` (= 3, see `yaogui_network_policy.h`) consecutive failures.
- The scan endpoint `GET /networks` (L388) already returns `{ssid, rssi, open}`, so **signal strength is already available**.
- Seven HTTP routes (`max_uri_handlers = 7`, L484): `/connect`, `/networks`, `/status`, `/api/reading`, `/guaxiang.html`, `/`, `/*`. New endpoints require bumping `max_uri_handlers` and the `handlers[]` array.
- The `portal.html` front end already uses `localStorage` (key `cyberyao.wifi.v2`) to cache passwords typed on the phone, for refill.

### 2.2 Hard constraints (violation means revert)

- **The provisioning entry can only be "open the hotspot automatically when it cannot connect".** Double-clicking Confirm must never pop the provisioning window directly; only prompt to go online when "fully offline with no connection attempt". This upgrade adds no manual pop-the-portal entry.
- **`GET /saved` and every endpoint must never return plaintext passwords**, only the SSID (plus whether encrypted, and priority).
- **The English CHANGELOG must not contain CJK characters** (a prior CJK word tripped the CI `check_repo.py` English prose gate).
- **The per-glyph font check stays**; a build must error out if a UI-required character is missing.
- **The reading page must never cache the user's reading inputs in `localStorage`** (question / name / options). Note: what `portal.html` caches here is the provisioning password refill, which is a separate thing and does not conflict.

## 3. Storage Layer Design (single pair -> array blob)

### 3.1 Data structure

Define a compact persisted structure inside `yaogui_time_sync.c` (fixed length, convenient for blob I/O and CRC checks):

```c
#define WIFI_CRED_MAX 5
#define NVS_CRED_BLOB "creds"      // new key, blob
#define NVS_CRED_VERSION 2

typedef struct {
  char ssid[WIFI_SSID_TEXT_SIZE];       // 33
  char password[WIFI_PASSWORD_TEXT_SIZE]; // 65
} wifi_cred_t;

typedef struct {
  uint8_t version;                 // = NVS_CRED_VERSION
  uint8_t count;                   // 0..WIFI_CRED_MAX
  wifi_cred_t items[WIFI_CRED_MAX]; // items[0] = highest priority
} wifi_cred_store_t;               // about 5 * 98 + 2 ~ 492 bytes
```

- **Priority equals array order**: `items[0]` is highest. `POST /reorder` reorders the array; after a successful `POST /connect` the SSID is promoted to `items[0]` (LRU-success, most recently successful first).
- The 24 KB NVS partition is far more than enough (a single blob < 512 bytes), so **no partition-table change**.

### 3.2 Read/write and migration

- `cred_store_load(store)`: read the blob; if absent or `version != 2`, try the legacy `ssid`/`password` keys, and if present migrate them into a `count = 1` new structure, write it back, then delete the legacy keys. This guarantees old devices do not lose provisioning after OTA.
- `cred_store_save(store)`: `nvs_set_blob` + `nvs_commit`.
- `cred_store_add(store, ssid, pwd)`: same-SSID overwrites the password and is promoted to the front; a new SSID is inserted at the front; when over `WIFI_CRED_MAX`, drop the tail (least recently successful). **Append, do not overwrite the whole table.**
- `cred_store_forget(store, ssid)`: delete the given SSID.

## 4. Connection State Machine (best-candidate reconnect)

Rework `handle_wifi_failure()` and the startup flow:

1. **Startup**: `cred_store_load`. If `count == 0`, `open_portal()`; otherwise enter the candidate-selection flow.
2. **Candidate selection** (new function `connect_best_candidate()`):
   - `esp_wifi_scan_start` (reusing the existing scan) to get nearby APs plus RSSI.
   - Build the candidate set = `saved intersect nearby-visible`, sorted by **RSSI descending** (ties broken by stored priority).
   - Try `connect_station()` in order; on a single failure (`WIFI_EVENT_STA_DISCONNECTED` or a 30 s timeout) move to the next.
   - Only when **all candidates are exhausted** does it `open_portal()`. This is the only hotspot-opening path, satisfying the "open hotspot only when it cannot connect" hard constraint.
3. **Success**: `handle_got_ip()` promotes the current SSID to `items[0]` and `cred_store_save`.
4. Reuse `yaogui_network_policy`: the meaning of `consecutive_wifi_failures` shifts from "failures on one AP" to "full-candidate-sweep failures"; the threshold logic is unchanged, and the unit tests are extended accordingly.

> Note: scanning in STA mode briefly interrupts the connection, so only scan on disconnect / startup / candidate switch, avoiding frequent scans while online.

## 5. HTTP API Design

| Method | Path | Purpose | Returns |
|---|---|---|---|
| GET | `/networks` | nearby APs (existing) | `[{ssid,rssi,open}]` |
| GET | `/saved` | **new**, saved list | `[{ssid,order}]`, **never a password** |
| POST | `/connect` | connect + append-save (changed to append, not overwrite) | `{accepted:true}` |
| POST | `/forget` | **new**, delete one credential | `{ok:true}` |
| POST | `/reorder` | **new**, reorder priorities | `{ok:true}` |
| GET | `/status` | connection progress (existing, to be extended) | `{state, ssid?}` |

- `max_uri_handlers` 7 -> 10, `handlers[]` gains 3 entries.
- `/forget` and `/reorder` bodies reuse `yaogui_form_value` parsing (`ssid=`; `/reorder` uses an index string such as `order=0,2,1`).
- `/status` gains a current target / connected `ssid` field so the phone can show "connecting to XXX".

## 6. Portal Front End (`portal.html`)

Building on the current "nearby Wi-Fi" list, the page presents two sections:

- **Saved (with signal)**: the intersection of `/saved` and `/networks`, with signal bars, a "delete" button, and drag / button reordering. Tapping reconnects directly (no need to retype the stored password).
- **Nearby new (new)**: APs with signal that are not saved; tapping opens the password form.

Interactions:

- Delete -> `POST /forget` -> refresh both sections.
- Reorder -> `POST /reorder` -> refresh.
- Connect to a new network -> `POST /connect` (append) -> poll `/status`.
- Stays pure-static, framework-free, gzip-stored (consistent with the current `portal.html`, minified + gzipped at build time).
- Keeps using `localStorage` to refill passwords (a provisioning convenience, not reading inputs, so it does not violate the constraint).

## 7. Two-Way Live Feedback

- **Phone side**: `/status` polling already exists; after adding the `ssid` field it shows "connecting to XXX / connected to XXX".
- **Device side**: `yaogui_app.c` already reads `connecting / local / online` through `wifi_status()`. This upgrade surfaces the "target SSID" to the view layer so the standby page / status bar can show "provisioning" or "connected to XXX". Invalidate and repaint only on state change to avoid needless refreshes (the same approach as the charging-icon optimization).

## 8. Test Plan

Extend `tests/test_yaogui_network_policy.c` (pure host unit tests, no hardware needed):

- Credential array: add dedup / overwrite-and-promote / over-limit tail eviction / forget / reorder.
- Legacy-format migration: single `ssid` + `password` -> `count = 1` new structure.
- Candidate ordering: saved intersect nearby, sorted by RSSI descending.
- `/saved` serialization carries no password (add a lightweight assertion, or gate it at the handler-review level).
- Keep the existing 5 cases green.

## 9. Risks and Open Questions

1. **Boundary for "full candidate sweep failed"**: clarify whether "scan failed" and "all candidates failed" both count toward `consecutive_wifi_failures`, to avoid scan jitter opening the hotspot by mistake.
2. **Scan latency**: scanning under APSTA jitters the connection; decide scan timeout and throttling at review (for example a 10 s minimum interval).
3. **`WIFI_CRED_MAX` value**: suggested 5, confirm at review.
4. **`/reorder` body format**: index string vs. SSID string, pick one; an index string is suggested (shorter, avoids CJK-SSID encoding issues).
5. **Flash advice**: flash only the `0x10000` app partition to keep NVS; but this change alters the NVS key structure, so the first boot triggers a one-time migration (legacy `ssid`/`password` -> `creds` blob), which is expected.

## 10. Implementation Order (after review approval)

1. Storage layer: `wifi_cred_store_t` + load/save/add/forget/reorder + legacy-format migration.
2. State machine: `connect_best_candidate()` reworking `handle_wifi_failure()` and the startup flow.
3. HTTP: `/saved`, `/forget`, `/reorder`, bump `max_uri_handlers`, extend `/status`.
4. Front end: two-section rebuild of `portal.html`.
5. Device-side live display of the target SSID.
6. Fill in unit tests + `idf.py build` + on-device flash verification.
