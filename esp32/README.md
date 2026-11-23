# ESP32-S3 firmware (Envoy reader + OCPP server)

The ESP32-S3 side of the solar charger. It does what the Java app does:

- joins Wi-Fi under a recognisable name (**`wattsync`**, shown in the router's device list like `wattsyncpi`)
- reads the Enphase Envoy meters over HTTPS every minute
- acts as the **OCPP 1.6J server** the wallbox connects to, with the same behaviour as the Java app
- has a **pause (kill switch)** that stops charging whatever the mode and survives restarts
- plans the charging current in **ECO**, **Max solar** (`MAX_SOLAR`, formerly BALANCED) and **Custom (fast)** modes with the same rules as the Java app: stay within the grid allowance (apart from riding out a dip at the minimum for up to 5 minutes), smoothing, start and stop timers
- pauses ECO and Max solar outside the **daylight window** (sunrise + 30 min to sunset - 1 h by default, both margins adjustable from the dashboard, calculated on the board for Lathrop, CA)
- stops both solar modes if the Envoy is silent for 150 s (**fail-safe**)
- plans **Cheapest hours** (`CHEAPEST`) with the same rules as the Java app: maximum current outside the expensive hours (the peak window), no meter needed; `solar::CheapHours` in `lib/solar_core/src/planner/CheapHours.h`, applied through the wallbox hold in `SolarController::applyCheapHours`
- **remembers its settings** (mode, Custom current, allowance, daylight toggle) in flash across restarts
- serves the **same web dashboard** as the Pi, at `http://wattsync.local/`: tiles, today's graph, an activity feed, charging sessions and savings (see the main README)
- keeps **a year of history** at lower detail the older it gets: every minute for 2 days, every 5 minutes for 30 days, every hour for a year, plus daily energy totals for a year and the activity log. Saved to flash every minute in a small journal, folded into a full save every 30 minutes, so a power cut loses at most a minute. The whole year takes about 0.4 MB of the 3.4 MB the board has; the dashboard's 7 days, 30 days and Year tabs and the CSV export read from it

- **remembers each charger** (status, transaction id, the limit it set) in flash, so after a restart or a power cut a charge in progress is picked up again straight away and shows on the dashboard; saved state older than an hour is ignored

- takes new firmware **over Wi-Fi** (`pio run -e esp32s3-ota -t upload`), protected by a password

- opens its own **setup network** (`WattSync-Setup`) when it has no Wi-Fi to join, or when you hold the BOOT button: from a phone you pick your Wi-Fi from a scanned list and paste the Enphase token, the Envoy address and the update password, with no cable and no app (section 1b)

**It now controls the wallbox on its own.** With the default settings it starts in ECO and acts on the solar surplus, exactly as the Pi does. Only one of the Pi and the ESP32 may be connected to the wallbox at a time.

Board: ESP32-S3-DevKit N16R8 (16 MB flash, 8 MB octal PSRAM), Arduino framework, built with PlatformIO.

## Code layout

| Folder | What it holds |
|---|---|
| `lib/solar_core/` | Portable logic with no hardware dependency, tested on the computer: `ocpp/` (frame builders, router, handlers), `wallbox/` (charger model, registry, controller), `planner/` (smoothing and the ECO/BALANCED rules), `daylight/` (sun calculation and window), `control/SolarController` (the poll cycle, fail-safe and settings), `web/` (the dashboard API: telemetry JSON and the mode, allowance and daylight endpoints), `setup/` (what the setup form may contain, the Wi-Fi change that is tried and undone, the scan list, the `/api/setup/...` endpoints), `state/`, `common/` (config, scheduler, logging) |
| `src/platform/` | Real clock, time zone offset and random numbers for the core, the flash settings store, the flash store for what is entered on the setup page, serial logging |
| `src/net/` | Wi-Fi with the device name and its rejoin watchdog (`WifiConnection::keepAlive`, rules in `lib/solar_core/src/net/WifiWatchdog.h`), the setup network (`SetupPortal`), network time, mDNS, OTA |
| `src/enphase/` | Envoy client and the meter poller (its own task, so the slow HTTPS call never delays the wallbox) |
| `src/ocpp_server/` | The WebSocket endpoint the wallbox connects to (transport only) |
| `src/web/` | The dashboard: HTTP server on port 80 and the embedded page, in front of the portable API (`lib/solar_core/src/web/ApiHandler`) |
| `src/console/` | Commands typed in the serial monitor (the same controls the dashboard will offer) |
| `src/main.cpp` | Wiring only |
| `test/test_core/` | Unit tests for the core |

This mirrors the Java app's packages (`ocpp`, `wallbox`, `charging`, `daylight`, `enphase`), so code in one can be compared with the other.

## 1. One-time setup (on the Mac)

PlatformIO is installed in a folder inside the project, so nothing is added to your system:

```bash
cd esp32
python3 -m venv .venv
.venv/bin/pip install platformio
```

Settings file (Wi-Fi name and password, and the same Enphase token the Pi uses). `src/secrets.h` already exists with placeholders:

```bash
open -e src/secrets.h
```

`secrets.h` is git-ignored. The Wi-Fi network must be **2.4 GHz**.

Since the setup network exists, these values are only **defaults**: anything saved from the setup page (section 1b) wins over `secrets.h`, field by field. You can still flash with real values in `secrets.h` and never use the setup page. With the placeholders left in, the board simply fails to join, and after 3 minutes the setup network opens.

The device name is `kHostname` in `src/config/DeviceConfig.h` (letters, digits and hyphens, 24 characters at most). It is both the DHCP name the router lists and the mDNS name (`wattsync.local`). Until you flash this version the board shows under the default name `esp32s3-XXXXXX`.

**If the Wi-Fi drops while running** (router restarted or off): the radio's own reconnect gets 15 s, then the board asks it to reconnect, then every minute joins again from scratch, and every fifth try also switches the radio off and on. It never restarts itself for this. The log says `Wi-Fi: still down, ...` and, when it returns, `Wi-Fi: back after N s`. Not used in Tesla mode (it has its own slower retry) or while the setup network is open.

## 1b. The setup network (change Wi-Fi and the Enphase details from a phone)

> Diagrams (start-up flow, the Wi-Fi trial, where each value comes from), screenshots of every state of the page, the endpoints and the code map are in [`../docs/setup-network.md`](../docs/setup-network.md).

The board can open a temporary Wi-Fi network of its own, **`WattSync-Setup`** (password **`wattsync`**, fixed for now), with a setup page on it. It works on any phone or computer, with no app and no cable, and it replaces editing `secrets.h` and reflashing when the router, the Enphase token or the Envoy address changes.

**When it opens**
- **No Wi-Fi is set** (first start with placeholders, or after `setup clear`): it opens at once and stays open until something is saved.
- **The Wi-Fi cannot be joined** for 3 minutes at start-up (router off, password changed): it opens, once per start. While it is open and nobody is on it, the board keeps retrying the home Wi-Fi every minute, and closes the setup network if that works.
- **By hand:** hold the **BOOT** button for 5 seconds while the board runs, or type `setup` in the console. This is how you change the token later while everything works.
- Except with no Wi-Fi at all, it closes after **15 minutes without use**.

**Using it (iPhone)**
1. Join the Wi-Fi **WattSync-Setup**, password `wattsync`. The phone usually opens the page by itself; if it does not, open `http://192.168.4.1/setup` in Safari.
2. The page lists the networks the board can see, strongest first (names repeated by a mesh or repeater appear once). Tap yours and type its password. Hidden networks cannot be listed: use *My network is not listed* and type the name. **Scan again** looks again; the page may stall for a few seconds while the board scans.
3. Paste the **Enphase token** (spaces, quotes and a leading `Bearer` are cleaned off), the **Envoy address** (empty = find it as `envoy.local`) and an **update password** (8 to 64 characters, for `pio run -e esp32s3-ota`).
4. **Save and restart.** Rejoin your usual Wi-Fi and open `http://wattsync.local`.

Leaving a secret field empty **keeps** what is saved: the page never shows a saved token or password, it only says one is saved. To change only the token, paste it and leave the rest.

**A Wi-Fi change is a trial.** After saving a different network the board restarts and tries it. If it joins, it keeps it. If not (wrong password, 5 GHz only, out of range) the **previous network is put back** and the setup network opens again with the reason on the page (`Could not join "X": wrong password. The previous Wi-Fi was put back.`). A failed attempt never leaves the board without a network it knew worked. Changes to the token, Envoy address and update password are kept either way.

**Safety**
- The page and its endpoints answer only while the setup network is open (403 otherwise). Changes need the `X-WattSync` header like the rest of the API, and secrets travel in the body of a POST, never in an address.
- No reply ever contains a secret. The saved values are in the flash's `net` area, which is **not encrypted**: someone holding the board can read them, exactly as they could read `secrets.h` values compiled into the firmware.
- **The setup network's password is the same on every board and written here**, so while it is open anyone within Wi-Fi range who has read this could join it and change the settings. That window is short (15 minutes, and only after a failure, a BOOT press or a console command), but it is real. A password derived from the board's own ID is the planned improvement.
- Saved values can be wiped with `setup clear`: the board restarts using `secrets.h` again.

**Limits**
- If the router's password changes **while the board is running**, nothing opens by itself; power-cycle the board (it opens after 3 minutes of failing) or hold BOOT.
- Only 2.4 GHz networks can be joined or listed. While the setup network is open the board answers a little slower, because the radio is shared with the home Wi-Fi.
- **Confirmed on the board (2026-10-26):** the setup network opens, an iPhone joins it and the page works. **Not yet tried:** the trial and undo of a bad password, saving a real change, and setup with the router off at start-up. The logic behind it (validation, trial and undo, scan list, endpoints) is covered by host tests; see the checks in section 4.

## 2. Connect, flash, watch

Plug a USB-C **data** cable from the Mac into the **UART/COM** port of the board. On the board this was tested with, that port appears as `/dev/cu.usbmodem...`; the name does not tell you which port you are on, so if you see the chip's boot lines but nothing from the program, you are on the wrong combination (see Troubleshooting).

```bash
ls /dev/cu.*
.venv/bin/pio run -e esp32s3 -t upload
.venv/bin/pio device monitor -e esp32s3
```

If the upload cannot connect, hold **BOOT**, tap **RESET**, release **BOOT**, and run it again. Press `Ctrl+C` to leave the monitor.

What you should see after boot:

```
=== ESP32-S3 solar charger ===
Name:   wattsync
INFO  Wi-Fi: connected in 2300 ms, IP 192.168.1.42, signal -41 dBm, device name "wattsync"
INFO  Time: 2026-10-14 22:06:49 PDT
INFO  mDNS: this board is wattsync.local
INFO  OCPP: listening on port 8080 (subprotocol ocpp1.6)
INFO  Point the wallbox at ws://wattsync.local:8080/pair  (or ws://192.168.1.42:8080/pair); it adds its own id
INFO  Settings: none saved yet, starting with defaults (ECO, 32 A custom, 1000 W allowance).
INFO  [DAYLIGHT] Today's window: 07:24 to 17:55
INFO  Envoy: envoy.local is 192.168.1.112 (312 ms)
INFO  mode ECO | solar 0 W, grid 633 W (importing), 4 s ago | charger: no charger connected | allowance 1000 W, ...
```

The OCPP server starts only after the network time is set, because OCPP replies carry the time and the wallbox adjusts its clock from them.

## 2b. Updating over Wi-Fi (no cable)

The board accepts a new firmware over Wi-Fi, so you can update it from the Mac without unplugging anything.

1. **Once, by cable:** flash this version the normal way (section 2). Only firmware that already contains the update feature can be updated wirelessly.
2. The password is `OTA_PASSWORD` in `src/secrets.h` (copy the line from `src/secrets.example.h` if your file lacks it, and pick a long random value).
3. Then, any time, with the board on the same Wi-Fi as the Mac:

```bash
env -u CPLUS_INCLUDE_PATH .venv/bin/pio run -e esp32s3-ota -t upload
```

The board's log shows `OTA: new firmware coming in...`, a line every 20 %, then `restarting`. The whole thing takes about 30 seconds.

What to know:
- **The charger keeps its last current while the board is busy.** The update blocks the controller for 10 to 20 seconds and then it restarts, so nothing is planned or read in that time. The wallbox keeps charging at the last limit it was given; after the restart the board adopts the running charge as it does after any restart. Updating when the car is not charging is still the cleanest.
- **Nothing recent is lost.** Just before the update starts the board saves its history to flash.
- **A bad image does not take over.** The new firmware is checked before the board switches to it, and a failed or interrupted transfer leaves the current firmware running (`OTA: update failed: ...` in the log). There is no automatic roll-back if a verified image misbehaves after it starts; for that, flash by cable.
- **The password is never printed or typed.** `scripts/ota_auth.py` reads it from `src/secrets.h` and passes it to the uploader.
- If `wattsync.local` does not resolve from the Mac, put the board's IP address in `upload_port` in `platformio.ini`.

## 3. The dashboard

Open **`http://wattsync.local/`** (or `http://<the board's IP>/`, printed at boot) from any device on the network. It is the same page as the Pi's dashboard: live solar, grid, charger and home power, the controller's state and reason, the mode buttons, the Custom current, the grid allowance slider and the daylight window toggle.

- The page is the Java app's `src/main/resources/dashboard.html`, copied in before every firmware build and embedded in the binary, so the two dashboards cannot drift apart. Change the page in one place and rebuild.
- The endpoints are the same as the Pi's (`GET /api/telemetry`, `POST /api/mode`, `/api/allowance`, `/api/daylight`) with the same messages and checks.
- Changes made on the dashboard are saved to flash like those made in the console.
- There is no password, as on the Pi: keep it on the home network. Every change (`POST`) must carry the header `X-WattSync: 1` (the dashboard adds it; with curl use `-H 'X-WattSync: 1'`), so a page on another site cannot change settings.
- The board also has `POST /api/maxamps`, `/api/goal` (departure goal: kWh by a time), `/api/notify` and `/api/notify/test` (messages to a webhook), and `/api/rate` takes the peak price and hours. They behave exactly as on the Pi, see the main README. Settings are kept in flash (NVS keys `maxAmps`, `peakCents`, `peakFrom`, `peakTo`, `peakWknd` (Cheapest hours: expensive hours also on weekends), `goalKwh`, `goalMin`, `notifyUrl`).
- **Tesla in the dashboard (experimental, board only).** Settings > Tesla pairs the car (type the VIN; the VIN travels in the body of a POST and is shown only masked), shows pairing progress ("tap your key card now"), and has a switch "Read the car every few minutes" (on by default once paired; read only), **Check now**, **Forget car**, and a collapsed **For testing** group with Start, Stop and a current box (each asks first, and "Put the setting back" appears after a current change). Once a car is paired a **Tesla** tile under the modes shows battery with the charge limit marked, charging state and amps, how old the reading is, and the charger's own amps for comparison. The controller never commands the car by itself yet: "Control through: Car" is not built. A read pauses the main loop for a few seconds (Bluetooth connect), so the watch runs every 5 minutes, not every minute. Endpoints: `POST /api/car/pair|refresh|watch|test|cancel|forget`, state in telemetry `car`. NVS `tesla`: `key`, `vin`, `watch`.
- **Tesla mode (board only, off by default).** Settings > Tesla > Tesla mode switches everything Tesla on or off and restarts the board. Off: Bluetooth is never started and no Tesla control is shown. On: Bluetooth works, the board also opens its own Wi-Fi **`wattsync-tesla`** (password shown in Settings > Tesla; open `http://192.168.4.1/` after joining) next to the home Wi-Fi, and a paired car is put in command instead of the charger. **Wallbox mode and Tesla mode are either-or:** with Tesla mode on the OCPP server is not started, so no wallbox connects or is controlled. A second link, **Connect through: Bluetooth | Tesla account** (the Tesla Fleet API over the internet), is chosen next to the switch; see `docs/tesla-fleet-api.md`. **The two modes are separate setups:** each has its own settings (mode, current, allowance, hours, prices, goal, messages) and its own history (graph, activity, sessions, savings); the first Tesla-mode start begins from defaults, and switching back shows the charger's own again. Only the Wi-Fi/meter/update credentials, the Tesla key and the hotspot password are shared. Away from home the board carries on without the home Wi-Fi, the Envoy and the internet (use Custom; the phone sets the clock). See `docs/tesla-car-control.md`.
- **Control through: Car (experimental, board only; the default stays Charger).** Settings > Tesla > *Control through* moves the command from the wall charger to the car: the planner (Eco, Max solar, Custom, Pause, departure goal, daylight window, fail-safe) then sets the car's current over Bluetooth and starts/stops its charging, and the charger is no longer commanded (only one is in command at a time). Switching to Car asks for confirmation in the page and needs `confirm=yes` in the API, and only works for a paired car; the choice is saved (NVS `tesla`: `through`, `onreturn`) and is dropped if the car is forgotten. *If the car can't be reached*: **Keep its setting** (default, silent), **Just tell me** (a message after 10 minutes out of reach, and when it is back) or **Stop when it is back** (also stops the charge once after a 10+ minute outage). Failed jobs retry after 30 s, 60 s, 2 min, then every 5 min; meanwhile the car keeps its last setting (it cannot be stepped down to the minimum first: nothing can be sent). **WattSync does not measure the car:** it is the only thing setting the current, so the draw it tells the planner is what it last set (the dashboard says "set to N A" rather than "charging at"). The car is read only to check that: after each command (the read-back), when WattSync takes command or after an outage, and **every 10 minutes** (a read pauses the main loop for a few seconds). If the check finds the car stopped by itself (charge limit, unplugged) or its current changed by someone else, the next decision corrects it. **Waking a sleeping car** is off until you tick "Allow WattSync to wake the car when charging is wanted" (Settings > Tesla): then it wakes the car only when charging is wanted and the car was last seen plugged in, at most once every 30 minutes (a wake keeps the car awake 10-15 minutes and uses a little battery). Without it a sleeping car is left alone and the tile says so. The test buttons (including **Wake now**) switch off while the car is in command. Console: `tesla wake`.
- Messages go out from a short background task so the main loop never waits. Secure (https) addresses are not certificate-checked, because the board holds no certificate list; messages are short status lines, never a password.
- **Start-up with no internet.** The board connects to Wi-Fi, starts the dashboard, mDNS and OTA at once, and gets the time in the background (pool.ntp.org, time.google.com, then the router). Charging control (OCPP server and meter polling) starts as soon as the clock is valid; until then the dashboard says so and *Diagnostics* shows the clock. Nothing waits on the internet at boot.
- If `wattsync.local` does not resolve on your computer, use the IP address.

## 4. Testing with the real wallbox

The wallbox can talk to only one controller, so while testing the ESP32 the Pi must not be controlling it.

1. Stop the Pi app: `ssh wattsync@wattsyncpi.local "sudo systemctl stop solar-charger"`
2. In the wallbox's settings, change the OCPP server URL to `ws://wattsync.local:8080/pair` (the wallbox adds its charger id itself; the older `/ocpp/wallbox/<chargerId>` URL also still works; if `.local` names do not work in the wallbox, use the IP address printed at boot).
3. Reboot the wallbox, or wait for it to reconnect. The monitor should show `Wallbox connected -> ID: ...`, then `BootNotification`, `StatusNotification`, and a status resync after 5 s.
4. Try the console commands below.
5. **When finished, change the wallbox URL back to the Pi and restart it:** `ssh wattsync@wattsyncpi.local "sudo systemctl start solar-charger"`.

### Console commands (type in the monitor, press Enter)

| Command | What it does |
|---|---|
| `status` | Mode, settings, daylight window, meters, charger, last command sent |
| `mode eco\|maxsolar\|fast\|cheapest` | Choose the charging mode (`fast` is the Custom current; `cheapest` is Cheapest hours; the old `balanced` still works) |
| `amps N` | Set the Custom current (6 to 40 A) and switch to fast mode. Starts a waiting car, changes the limit while charging, and starts a car plugged in later |
| `allowance W` | Grid allowance, 0 to 1000 W (ECO and Max solar) |
| `daylight on\|off` | Pause ECO and Max solar outside the daylight window, or not |
| `pause` | The kill switch: stop charging and keep it stopped whatever the mode, until `resume`. Saved across restarts |
| `resume` | End the pause: back to the mode that was set before |
| `stop` | Stop the charge now (in ECO and Max solar the planner may start it again by its normal rules) |
| `ask` | Ask the charger for its status and meter values |
| `rate C` | Electricity price in cents per kWh (0 to 200), used for the savings |
| `wifi` | Which network the board is on, the signal and the IP (never the password) |
| `setup` | Open the setup network (section 1b) |
| `setup clear` | Forget everything saved from the setup page and restart: `secrets.h` applies again |
| `tesla scan [seconds]` | Phase 0 of Tesla control, no VIN needed: list every Tesla in range (name hash, signal, how often), strongest first, for 10-300 s (default 60). Connects to nothing, writes nothing |
| `tesla scan <VIN> [seconds]` | The same for your own car only, with a clear verdict. The VIN is not saved. `tesla stop` ends a scan, pairing or status early |
| `tesla pair <VIN>` | **Changes the car's key list.** Makes this controller's own key (first time only), connects to the car with that VIN, asks to add the key with the *Charging Manager* role, and waits up to 2 minutes while you **tap your key card on the centre console** (and confirm on the screen if asked). On success the VIN is saved on the board. Only the masked VIN is ever printed. Needs the car in range and awake-ish; the key shows in the car under Controls > Locks, where you can rename or delete it |
| `tesla status` | Read-only: asks the car's security computer (works asleep), and if the car is awake opens a session and reads battery, charge limit, charging state and current. **Never wakes the car and sends no charge command.** This is the compatibility test for a model and year |
| `tesla key` / `tesla forget` | Shows the key's short fingerprint and whether a car is paired / deletes the key and VIN from the board (remove the key in the car too) |
| `tesla charge start` / `tesla charge stop` | **Changes the car.** Starts or stops its charge over Bluetooth, then reads the charge state back. Never wakes a sleeping car (it says so and sends nothing). The car must be paired, plugged in and awake. Do not use while the real wallbox is in control |
| `tesla wake` | **Changes the car.** Wakes a sleeping car: looks first (an awake car is left alone), sends one signed wake command, then asks the car for its status every few seconds for up to about 20 s. Needs the car paired and in range |
| `tesla charge amps <6-40>` | **Changes the car.** Sets the charging current the car asks for, then reads it back. Same rules as above |
| `tesla debug on` / `tesla debug off` | Prints every message to and from the car as hex (no secrets in them). Off at every start; use it when trying a new car model |
| `tesla mode on\|off` | Tesla mode (off by default): Bluetooth, the `wattsync-tesla` Wi-Fi and the car in command; restarts the board. Every other `tesla ...` command refuses while it is off |
| `tesla selftest` | Runs Tesla's published example values (key agreement, message check, one encrypted command) through the board's own cryptography. No car needed |
| `help` | The list |

Settings changed here are saved to flash at once.

### What to check

| Check | Pass |
|---|---|
| Wallbox connects and the handshake is accepted | `Wallbox connected` in the log, stays connected for 10+ minutes with a heartbeat each minute |
| Status tracking | Plug the car in and out; `status` follows (`Preparing`, `Charging`, `Finishing`) |
| Start | Car plugged in, type `amps 10`: `RemoteStartTransaction (10A)`, then `answered ...: Accepted`, then Charging |
| Change limit | `amps 16` while charging: `SetChargingProfile (16A, txn ...)`, real current follows |
| Stop | `stop`: `RemoteStopTransaction`, charging ends |
| ECO on real sun | `mode eco` on a sunny day: poll lines `[ECO] solar ... => N A (reason)` match what the Pi would log for the same readings; it starts after about 3 minutes of surplus |
| Allowance | `allowance 0` then `allowance 1000`: the next poll line shows the new allowance and the available amps change at once |
| Night pause | `daylight on` after sunset (or before sunrise): `[DAYLIGHT] Outside the daylight window`, a running ECO or Max solar charge stops once, no Envoy reads |
| Fail-safe | Unplug the Envoy for over 3 minutes: `[FAILSAFE]`, charging stops; plug it back: `reachable again` |
| Saved settings | Change mode and allowance, press RESET: the boot log says `Settings: restored ...` |
| Restart mid-charge | While the car charges, press RESET on the board: after the wallbox reconnects the log says `Restored last known state of charger ...`, the dashboard keeps showing the charge (Car: Charging, Charging at the right limit), and ECO/BALANCED/Custom carry on without stopping the car |
| Kill switch | Press **Pause charging** on the dashboard (or type `pause`) while the car charges: it stops within a second and the banner says charging is paused. Press RESET on the board: it comes back still paused and does not restart the car. **Resume charging** returns to the earlier mode |
| Dashboard | `http://wattsync.local/` loads, shows live values and updates every few seconds; switching mode, moving the allowance slider and the daylight toggle show a confirmation and match `status` in the console |
| Measured current | The `status` line shows `n A measured` if the wallbox reports it (same as the Java app's `measured` label) |
| Reconnect | Power-cycle the wallbox: it reconnects, a status resync follows, no duplicate sessions |
| Setup by hand | Hold BOOT for 5 s (or type `setup`): the log says `Setup: ... the setup network is open`; `WattSync-Setup` appears on the phone; the page lists networks within a few seconds and lists yours |
| Setup, nothing changes | Open it, change nothing, close it (wait 15 min or restart): the board's Wi-Fi, charging and dashboard are unaffected |
| Setup, wrong Wi-Fi password | Choose your network with a wrong password and save: the board restarts, logs `Could not join ...: wrong password. The previous Wi-Fi was put back.`, rejoins the old network, and the setup page shows the same message |
| Setup, good change | Save the same network with the right password: it restarts, joins, and `wifi` shows the network; the setup network is gone |
| Setup, token only | Paste a new token (everything else empty): no Wi-Fi trial; after the restart the Envoy reads work (`mode ...` line shows fresh readings) |
| Setup, no Wi-Fi at all | `setup clear` with placeholders in `secrets.h`: the setup network opens at once and stays open; after saving a real network the dashboard is back |
| Setup, router off at boot | Power the board with the router off: after 3 minutes the setup network opens; switch the router on: the board joins by itself within a minute |
| Phone setup sheet | Join `WattSync-Setup` on the iPhone: the setup page opens by itself, or `192.168.4.1/setup` works in Safari |

Anything different from what the Pi does is a bug in this port; send me the log.

## 5. Unit tests (no board needed)

```bash
.venv/bin/pio test -e native
```

250 tests in ten groups: `test_core` (OCPP frames checked character by character against the Java app's output, routing, handlers, the wallbox controller, reconnects), `test_planner` (smoothing and the ECO/BALANCED rules), `test_notify` (messages), `test_daylight` (sunrise and sunset against published values for London, New York, Lathrop, Sydney and the poles, plus the window), `test_control` (the poll cycle, fail-safe, night pause, mode switching and saved settings), `test_api` (the dashboard's telemetry JSON and POST endpoints, including every rejected input) `test_persistence` (saving and restoring a charger's state across a restart) `test_history` (samples, energy totals and savings split, the activity log, charging sessions, saving and loading) `test_tesla_protocol` (the Tesla message format, Tesla's published example values for key agreement and encryption, pairing and status against a pretend car) `test_tesla` (the VIN rule, the car's Bluetooth name checked against Tesla's published example, the signal verdict, the list of cars heard) and `test_setup` (what the setup form may contain, token and address cleaning, the Wi-Fi change that is tried and undone, the scan list, and the setup endpoints, including that no reply contains a secret).

**On this Mac** the compiler cannot find the C++ standard library headers (a Command Line Tools install problem, unrelated to this project). If the tests fail with `'string' file not found`, run them like this (only for the tests; do not use it for the firmware build):

```bash
CPLUS_INCLUDE_PATH=/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include/c++/v1 .venv/bin/pio test -e native
```

## Behaviour worth knowing

- The OCPP reply layout is `[3, id, action, payload]`, copied from the earlier project that works with this wallbox (one place to change: `callResult` in `lib/solar_core/src/ocpp/OcppMessages.cpp`).
- A charger that stays silent for 5 minutes (it normally beats every 60 s) is disconnected so it can reconnect cleanly. A charger reconnecting under the same id replaces its old connection.
- Remote starts are limited to one every 30 s per charger, as in the Java app.
- The household voltage used to turn watts into amps is 240 V (`kHouseholdVoltage` in `lib/solar_core/src/common/Config.h`). The location for the daylight window is Lathrop, CA (`kSiteLatitude` and `kSiteLongitude` in the same file); the time zone comes from `kTimezone` in `src/config/DeviceConfig.h`.
- Values saved from the setup page win over `secrets.h` field by field (the Wi-Fi name and its password go together). The Envoy address saved as empty means "find it automatically" even if `secrets.h` has an address. With no update password anywhere, updating over Wi-Fi is switched off rather than left open.
- All the tuning values (allowance, start and stop levels, timers, smoothing) are in `lib/solar_core/src/common/Config.h` and match the Java app's `AppConfig`.
- The Envoy is read once per poll interval and only when needed: not with no charger connected, and not outside the daylight window in ECO and Max solar. In Custom (fast) mode it is read for the dashboard only and never changes the current.

## Troubleshooting

**Finding out why the board restarted.** With no cable attached, open the dashboard's gear, then *Diagnostics*: it shows the restarts and crashes counted, the last crash with its time, and free memory. On the serial monitor: The boot banner prints `Last restart: ...`. `power on`, `software restart` and `reset pin` are normal. A line starting with `!!!` (`CRASH`, a watchdog, or `BROWNOUT`) means it restarted on its own; a brownout points to the power supply or cable. A status line every minute also prints free memory (`Memory: ... heap free, largest block ...`), so a slow memory leak shows as the numbers falling. To keep a log overnight, run `.venv/bin/pio device monitor -e esp32s3 --filter esp32_exception_decoder --filter time --filter log2file` (it needs a real terminal, so it cannot be piped into `tee`); a crash then prints a `Backtrace:` with function names, saved in `platformio-device-monitor-*.log`. Keep the Mac awake with `caffeinate -dims`.

- **Boot loop or `PSRAM ID read error`:** the board does not have octal PSRAM. In `platformio.ini` change `board_build.arduino.memory_type` to `qio_qspi` and remove `-DBOARD_HAS_PSRAM`.
- **Boot lines only, nothing from the program:** the log goes to the UART/COM port with the default build (`esp32s3`). If the cable is in the other, native USB port, use `-e esp32s3-usb` for both upload and monitor.
- **The dashboard does not load:** check the boot log for `Dashboard: http://wattsync.local/`; try the IP address; make sure the computer is on the same network.
- **Wi-Fi fails to connect:** the log says why (`wrong password` or `network not found`, the board only joins 2.4 GHz networks). After 3 minutes the setup network opens (section 1b); fix the details there.
- **`envoy.local` not found:** enter the Envoy address on the setup page (or set `ENVOY_IP` in `src/secrets.h`).
- **The setup page says "Setup is off":** the setup network is closed. Hold BOOT for 5 seconds or type `setup`.
- **The setup page stalls while scanning:** expected for a few seconds; the board scans on the radio the page uses. Wait, or press Scan again.
- **The wallbox connects, then drops:** send me the monitor log from the moment it connects.
- **Device name still shows `esp32s3-XXXXXX` on the router:** reboot the board, then refresh the router's list (some routers cache the old name for a while); check the `device name` shown in the Wi-Fi line of the log.
- **Permission denied on the serial port:** close any other program using it.
