# OTA Setup for Firebird Multi-Gauge

Local-network over-the-air firmware updates for the assembled gauges.
Once set up, you never touch USB again unless something catastrophic
happens.

## One-time setup

### 1. Create your local `ota_config.h`

The file `main/OTA/ota_config.h` contains your WiFi credentials and
firmware server URL. It is **gitignored** — your credentials never get
committed.

```
cp main/OTA/ota_config.h.example main/OTA/ota_config.h
```

Edit `main/OTA/ota_config.h` and fill in:

- `OTA_WIFI_SSID`     -- your home WiFi name
- `OTA_WIFI_PASSWORD` -- your home WiFi password
- `OTA_FIRMWARE_URL`  -- `http://<your-PC-ip>:8000/firmware.bin`

On Windows, find your PC's IP with `ipconfig` in PowerShell -- look
for the IPv4 address on your active network adapter (usually
192.168.x.x or 10.0.x.x).

### 2. USB flash each gauge ONCE with the new firmware

The OTA-capable firmware has a different partition table (two app
slots + storage instead of one big app slot), so the first install
**must** come over USB. After this, all subsequent updates happen
over WiFi.

```
idf.py build
idf.py flash monitor    # connect each gauge, flash, watch boot log
```

After flashing:

- The gauge boots normally.
- NVS is wiped -> re-enter your saved settings (overheat trip, splash
  time, brightness, buzzer) once per gauge.
- Open settings, swipe through and reconfigure your three settings.

Repeat for the second gauge.

### 3. Verify WiFi connects

You can sanity-check that WiFi credentials are right BEFORE assembling
the gauges into the pod:

1. Power up the gauge.
2. Open settings.
3. Tap the **OTA** button.
4. Watch the status text under the cards. If it stays on "Connecting
   WiFi..." for more than 15 seconds, the SSID or password is wrong
   in `ota_config.h`. Fix and re-flash.

If WiFi connects, but the download fails ("Server unreachable"), the
PC IP is wrong or the firmware server isn't running. We test that
next.

## Day-to-day update workflow

Now you're set up. To push a code change to both gauges:

### On your PC

```
cd C:\Firebird-Gauges\ESP32-S3-Multi-Gauge

idf.py build                        # builds the new firmware

python scripts/serve_firmware.py    # starts HTTP server on port 8000
```

The server prints the URL it's serving from -- should match what's in
your `ota_config.h`.

Leave the server window open.

### On each gauge

1. Open **settings** (long-press any gauge screen).
2. Tap **OTA** button.
3. Watch the status:
   - "Connecting WiFi..." -- ~3-8 seconds
   - "Downloading 12%..." -- updates as the firmware downloads (~30 s)
   - "Installing..." -- a few seconds
   - "Rebooting!" -- gauge restarts with new firmware
4. Repeat on the second gauge.

### Back on your PC

Press **Ctrl+C** in the firmware server terminal to stop it (not
strictly necessary, but tidy).

## Total iteration time

- Code edit + build: 30 sec - 2 min depending on what changed
- Start server: 1 sec
- OTA on first gauge: 30-40 sec
- OTA on second gauge: 30-40 sec

**You can go from "I just wrote a fix" to "both gauges are running
the fix" in about 2-3 minutes.**

## Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| "WiFi timeout" on gauge | Wrong SSID/pass in ota_config.h | Edit + USB reflash |
| "Server unreachable" | PC IP wrong, or firmware server not running, or Windows firewall blocking port 8000 | `ipconfig` to verify IP; ensure `python scripts/serve_firmware.py` is running; allow Python through Windows Defender |
| "Invalid image" | Server is serving the wrong file (e.g. left-over from another project) | Re-run `serve_firmware.py` -- it always re-copies the latest build |
| "Download failed" mid-way | WiFi dropped, or PC went to sleep | Wake the PC, retry from settings |
| New firmware boots, then reverts to previous version | New firmware crashed 3 times -> rollback fired. Check the boot log over USB to diagnose | Fix the bug, OTA the corrected build |
| Gauge stuck on splash after OTA | Firmware bug -- crashed before OTA_Init() could mark slot valid. Wait for 3rd reset -> rollback kicks in automatically | If rollback doesn't fire, USB-flash a known-good build |

## How it works (briefly)

1. The flash is divided into two **app slots** (ota_0, ota_1) of 4 MB
   each, plus a tiny **otadata** partition that tells the bootloader
   which slot is currently active.
2. The running firmware downloads a new binary into the INACTIVE slot
   -- the active slot is untouched.
3. When the download completes, the firmware updates `otadata` to
   point at the new slot and reboots.
4. New firmware boots; if it runs OK for long enough to call
   `esp_ota_mark_app_valid_cancel_rollback()` (which happens at boot
   in `OTA_Init()`), the new slot is marked valid.
5. If the new firmware crashes before that mark, the bootloader
   notices a non-zero crash counter and reverts to the previous slot
   on the next reset.

This means a broken OTA never bricks the gauge -- worst case is one
reboot cycle and you're back to the previous version, then you can
USB-fix the bug and try again.

## Security notes

- The OTA path uses **plain HTTP, no auth**. Anyone on your local
  WiFi could push their own firmware to your gauges. For a hobby
  install in your garage this is acceptable. If your gauges are ever
  on a public WiFi or a network you don't control, use HTTPS with a
  pinned cert -- esp_https_ota supports it.
- `ota_config.h` is `.gitignore`-d so your WiFi password never lands
  in a public GitHub repo. Don't disable that ignore.
