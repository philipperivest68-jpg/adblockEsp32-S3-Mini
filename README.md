# S3MiniPihole - ESP32-S3 Mini AdBlock

A network-wide ad/tracker blocker on a tiny **ESP32-S3 Mini**. It runs as a **DNS
server** for your home: normal domains resolve as usual, ad/tracker/malware domains
get answered with `0.0.0.0` so they never load. One small box, every device on the
WiFi benefits, no app to install on phones or TVs.

~415,000 domains (ads, trackers, malware) live in flash as
40-bit hashes in a bucketed index, memory-mapped and looked up on the device -
**no PSRAM needed**.

- Blocklist memory-mapped from a raw flash partition, ~8 us per lookup (~123k qps)
- Web dashboard: stats, per-device counters, recently/top blocked domains, 24 h
  history, pause, add your own blocked domains
- Phone-based setup, no computer needed by the end user
- Blocklist updates over WiFi (upload or auto-fetch from a URL); firmware flashed over USB

> Fork of [NikoMalik/esp32-block](https://github.com/NikoMalik/esp32-block), itself a
> fork of [M-Abozaid/esp32-c3-adblock](https://github.com/M-Abozaid/esp32-c3-adblock).
> This fork targets the **ESP32-S3 Mini** (default `s3` env); the classic ESP32
> (`esp32dev`) and ESP32-C3 (`c3`) envs are still there.

---

## What you need

- An **ESP32-S3 Mini** board (ESP32-S3-MINI-1 / FH4R2 module, Lolin S3 Mini,
  Waveshare S3-Zero, ...), **4 MB flash**, PSRAM not needed
- A **USB-C cable** and any USB power source (a phone charger is fine)
- A **2.4 GHz** WiFi network (ESP32 does not see 5 GHz)
- A phone to do the one-time setup

The box only uses USB for power and the initial flashing. It joins your WiFi wirelessly.

On the host: [PlatformIO](https://platformio.org/) (`pio`). It bundles `esptool`.
`make` is optional (Linux/macOS convenience).

---

## Quick start (end user)

The box comes already flashed. You only connect it to your WiFi:

1. Plug the box into USB power. After ~30 seconds without a known WiFi it creates its
   own WiFi network.
2. On your phone, open WiFi settings and join **`S3MiniPihole`**
   (password: whatever came with the box - `AP_PASS` in `secrets.h`; open if empty).
3. A setup page opens automatically. If not, open `http://192.168.4.1` in a browser.
4. Pick your home WiFi from the list (or type its name), enter its password, tap
   **Connect**.
5. The box reboots and joins your network. Switch your phone back to your normal WiFi.

Done - the box is online. Leave it plugged into power.

### What happens when the WiFi is gone

- **At boot:** it tries the saved WiFi for 30 s. If that fails it opens the
  `S3MiniPihole` setup hotspot. If a network is already saved and nobody joins the
  hotspot for 3 minutes, it reboots and retries - so after a power cut it reconnects
  on its own once the router is back.
- **While running:** if the WiFi drops, it keeps retrying the same network (driver
  auto-reconnect); it does not switch to the hotspot. Power-cycle it if it stays offline.
- **Move it to another network:** hold **BOOT** while plugging it in (wipes the saved
  WiFi and opens the hotspot), or use *Forget WiFi* on the dashboard.

### Make it block ads on every device (point your router at it)

The box is a DNS server: a device only filters ads if its DNS is set to the box.
The clean way is to set it once on the **router** so every phone/TV/laptop uses it
automatically.

**1. Find the box's IP.** Open `http://S3MiniPihole.local` (or check the dashboard's
`IP` field, your router's "connected devices" list - it shows up as `S3MiniPihole` -
or the serial log at boot: `WiFi up: <ip>`). Example: `192.168.1.50`.

**2. (recommended) Reserve that IP for the box.** In your router admin, find
*DHCP reservation* / *static lease*, and bind the box's MAC to its current IP. This
stops the address from changing later and breaking DNS for everyone.

**3. Set the router's DNS to the box.** In the router admin, open the LAN / DHCP
settings and set **DNS server** to the box's IP (`192.168.1.50` in the example).
Where exactly depends on the brand:
- it's usually under *Network / LAN / DHCP server*, a field called *DNS 1* / *Primary DNS*
- set DNS 1 = the box; leave DNS 2 empty, or set a fallback like `9.9.9.9` (note: if
  you add a fallback, devices may use it and skip filtering - for strict blocking
  leave only the box)
- save and reboot the router (or just reconnect a device) so the new DNS is handed out

**Prefer not to touch the router?** Set DNS to the box on a single device instead
(phone/laptop WiFi settings -> DNS -> manual -> the box's IP). Only that device filters.

> Router requirements: a normal home router with a 2.4 GHz network and a settable DNS
> (almost all have it). No special hardware. If the box reboots and devices use only
> it for DNS, the internet pauses until it's back - that's why a reserved IP + leaving
> it powered matters.

---

## Dashboard

Once the box is online, open **`http://S3MiniPihole.local`** (or its IP) in a browser
on the same network.

- **Stats** - total blocked/allowed, block rate (%), domains in the list, signal,
  temperature, free RAM, uptime
- **Last 24 hours** - one bar per hour, blocked (red) vs allowed (green); hover a
  bar for its counts and block %
- **Top blocked domains** - the 20 most-blocked domains with hit count and last hit
- **Recently blocked** - the last 50 blocked queries: when, domain, which client,
  record type (A, AAAA, HTTPS, ...) and why (on the blocklist, or the client is
  banned). Filter by domain or client IP; *Clear* empties this and the top list.
- **Clients** - per-device blocked/allowed counters and block %, ban a device
- **Pause** - stop blocking for N seconds (e.g. to debug a site), then auto-resume
- **Add a blocked domain** - type a domain and it is blocked immediately (stored on
  the box). Use this when something you don't want isn't caught by the lists.
- **Unblock** - remove a domain you added
- **Blocklist update** - upload a new `blocklist.bin`, or set a URL to auto-fetch on a
  schedule (see [Remote blocklist](#remote-blocklist))

There is no dashboard login: anyone on your network can use the controls.
State-changing actions require a custom request header, so other websites can't
trigger them from a visitor's browser (CSRF).

The blocked-query history lives in RAM (~10 KB) and resets on reboot; nothing is
written to flash. Top-domain hit counts are close estimates: 64 domains are
tracked and the least-hit one is replaced when a new one arrives. Domains longer
than 71 characters are truncated, and a subdomain blocked via its parent (e.g.
`x.ads.com` via `ads.com`) is shown as the name that was queried. The raw data is
at `/blocked.json`.

---

## Build & flash (developers)

```bash
# 1. credentials: copy the template and fill it in
cp src/secrets.example.h src/secrets.h
#    WIFI_SSID/WIFI_PASS  - optional fallback (setup is usually done from the phone)
#    AP_PASS              - setup-AP password you hand out (>=8 chars, or "" = open)

# 2. build + flash the firmware (s3 is the default env)
pio run -e s3 -t upload --upload-port COM23        # Linux/macOS: /dev/ttyACM0

# 3. write blocklist.bin into the raw 'blocklist' partition at 0x170000
python ~/.platformio/packages/tool-esptoolpy/esptool.py --chip esp32s3 \
    --port COM23 --baud 460800 write_flash 0x170000 blocklist.bin

# 4. watch it boot / find its IP
pio device monitor -e s3 -p COM23                  # look for "WiFi up: <ip>", Ctrl+C to exit
```

The S3 Mini uses its native USB port, so serial output goes over USB CDC
(`ARDUINO_USB_CDC_ON_BOOT=1`). If upload can't connect, hold **BOOT**, tap **RST**,
release BOOT, and retry.

The `s3` env pins `platform = espressif32@6.9.0` (Arduino 2.x / IDF 4.4). Newer
pioarduino platforms (IDF 5) change the `esp_partition_mmap` API and don't build
this code as-is.

With `make` (Linux/macOS): `make upload`, `make blobflash`, `make flash` (both),
`make monitor`, `make dev`, `make erase`, `make blocklist`. Defaults: `ENV=s3`;
override like `make flash PORT=/dev/ttyACM0`. `blobflash` picks the chip from `ENV`
(`s3` -> `esp32s3`, `c3` -> `esp32c3`, `esp32dev` -> `esp32`), e.g.
`make flash ENV=c3 PORT=/dev/ttyACM0`.

**ESP32-C3** (4 MB flash) works too: `pio run -e c3 -t upload --upload-port COMxx`,
then write the blocklist with `--chip esp32c3` (same `0x170000` offset). Cheap C3
"Super Mini" boards often have weak antennas; if one won't join your WiFi, lower
its TX power (`WiFi.setTxPower(WIFI_POWER_8_5dBm)`).

Firmware changes go over USB (single-app layout, no firmware OTA). The **blocklist
updates over WiFi** from the dashboard - it is written straight into its flash
partition.

---

## Configuration (`secrets.h`)

Copy `src/secrets.example.h` to `src/secrets.h` (gitignored) and set:

| field | what it is | required? |
|---|---|---|
| `WIFI_SSID` / `WIFI_PASS` | your home WiFi, used as a **fallback** if the box has no saved network. Leave as `YOUR_WIFI_SSID` to skip - setup is normally done from the phone (captive portal). Handy if you flash for your own network. | optional |
| `AP_PASS` | password for the box's **setup WiFi** (`S3MiniPihole`) shown during provisioning. `>=8` chars turns on WPA2 so only someone you gave it to can open the setup page; **empty string = open** (anyone nearby can). This is the password you hand out with the box. | recommended |

The device name (setup hotspot SSID, DHCP hostname, mDNS `.local` name, dashboard
title) is `DEVICE_NAME` in `src/wifi_portal.h`.

There is no dashboard login (see [Dashboard](#dashboard)), so `AP_PASS` is the only
security setting; WiFi is usually set from the phone, not here. (There is no `OTA_PASS` - network firmware update was removed; firmware is
flashed over USB.)

---

## Blocklist

Built on the host by `tools/build_blocklist.cpp` (compiled by `make blocklist`, needs
a C++ compiler): it downloads the default lists - global aggregators **OISD Big** and
**Hagezi Pro++** and the **StevenBlack** base - extracts domains, hashes them with fxhash to 40 bits, dedups, and writes a v2
bucketed container (`C3BL` magic + version + CRC32 header, then a 65536-bucket index
+ 3-byte suffixes, see `src/blfmt.h`). Edit `DEFAULT_SOURCES[]` to add/remove lists
(any hosts or AdGuard/ABP `||domain^` source works; cosmetic/regex rules are skipped).

No C++ compiler (e.g. Windows)? `tools/build_blocklist.py` is a byte-identical
Python port: `python tools/build_blocklist.py blocklist.bin`. Keep its
`DEFAULT_SOURCES` in sync with the C++ tool.

Watch the size: the partition holds ~740k domains (2.375 MB). The Russian
NoADS_RU lists were dropped because they grew to ~2.5M entries each and pushed
the blob to 8.8 MB, which no longer fits.

On the device the blob lives in its own raw `blocklist` partition (2.375 MB -> room
for ~740k domains) and is memory-mapped; lookups are a bucket index + a short scan.
A small LittleFS holds runtime state (custom domains, bans, update config).

> Only this v2 `C3BL` format works. The 5-byte flat `blocklist.bin` from
> esp32-c3-adblock is a different format and will be rejected.

### Remote blocklist

Host a prebuilt `blocklist.bin` and the box pulls it over HTTPS (redirects followed,
cert not pinned) - on demand with *Fetch now*, then every 24 h. The URL must point to
the **raw file**, e.g.

```
https://raw.githubusercontent.com/philipperivest68-jpg/blocklist/main/blocklist.bin
```

(not a `github.com/.../blob/...` page - that returns HTML). A good fetch logs
`[remote] ok: <N> domains`.

Heads-up: once the server answers HTTP 200, the partition is erased and the new file
is streamed in, then header + size + CRC are checked. If the download breaks midway,
the old list is already gone and the box forwards everything unfiltered until the
next good fetch, an upload, or a USB `blobflash`.

---

## Why it's fast (architecture)

Flash layout (4 MB, single app slot):

```
nvs        20 KB      WiFi creds (NVS)
app0       1.375 MB   firmware
blocklist  2.375 MB   raw partition @ 0x170000: the hash blob, memory-mapped
littlefs   192 KB     runtime state (custom / bans / update config)
```

**Small.** A domain is never stored as text. Each is hashed to a 40-bit value
(fxhash) - that is already just 5 bytes instead of a ~20-char string. Then the hash
is split into a 16-bit prefix and a 24-bit suffix, and the list is grouped into
65536 "buckets" by prefix. The prefix is **not stored**: the bucket a suffix lives
in already encodes it (position = data). Only the 24-bit suffix is written =
**3 bytes/domain**. So ~415k domains take ~1.4 MB, versus ~2.5 MB of raw text that
also wouldn't fit in RAM to search.

**Fast.** A lookup is three cheap steps - no filesystem, no PSRAM, no string compares:

1. `fxhash(domain)` -> a 40-bit value (fast non-crypto hash)
2. the top 16 bits pick one of 65536 buckets via a flat offset table - O(1), no
   binary search
3. scan that bucket's 3-byte suffixes (avg ~10, sorted) for a match

The blob is **memory-mapped** (`esp_partition_mmap`): the CPU reads it straight
through the flash cache like RAM, so steps 2-3 are just a few cache-line reads.
That is the main speed win. The earlier design kept the list as a LittleFS file and
did one random filesystem read per lookup (~1.2 ms); mmap + buckets dropped that to
8 us - 150x faster, ~123k lookups/sec.

Modules: `blocklist` (hash + mmap lookup), `dns` (server, forward upstream),
`wifi_portal` (connect + captive portal), `web` (dashboard), `clients` (per-device
table), `querylog` (recent/top blocked domains + hourly history). `main.cpp` is just `setup()`/`loop()`.

---

## Notes

- Upstream resolver is Quad9 (`9.9.9.9`); change via the `UPSTREAM_IP` build flag.
- No PSRAM required; works on a bare 4 MB ESP32-S3.
- Harmless at boot: `No core dump partition found!` (the layout has none) and
  LittleFS "does not exist" lines for `custom.txt` / `banned.txt` / `update.cfg`
  until you first use those features.
- Security: the dashboard has no login, so anyone on your network can change
  settings - only put the box on a network you trust. Set a real `AP_PASS` before
  giving the box to anyone.
