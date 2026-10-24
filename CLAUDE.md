# WattSync (folder and jar still named SmartEnergyApp)

A solar EV-charging controller. It reads the house's solar, home and grid power from an Enphase Envoy gateway,
decides how fast a wallbox should charge the car, and talks to the wallbox over OCPP 1.6J. Two implementations
share one dashboard page. Only one of them can be connected to the wallbox at a time.

- **Pi**: Java 17 / Javalin app (this folder). Host `wattsyncpi`, dashboard and OCPP both on port 8080.
- **ESP32-S3**: C++ firmware in `esp32-poc/` (PlatformIO, Arduino). Host `wattsyncesp`, dashboard on port 80,
  OCPP on port 8080 at `/ocpp/wallbox/<id>`, OTA on 3232. Board: ESP32-S3 N16R8 (16 MB flash, 8 MB octal PSRAM).
- Site: Lathrop, CA (37.8412, -121.2691, America/Los_Angeles). The array faces south-east (118 degrees), no shade.
- Wallbox charger id: `mywallbox/wattsyncpi`. It needs RemoteStartTransaction to start; SetChargingProfile only changes
  the limit of a running transaction. Reply frames are deliberately `[3, id, action, payload]` (copied from an earlier
  working project; do not "fix" them).

## Layout

```
src/main/java/com/example/   Pi app: charging (planner), daylight, enphase, history, ocpp, persistence, state, wallbox, web
src/main/resources/dashboard.html   THE dashboard, one file, served by the Pi and embedded in the firmware
src/main/resources/favicon.png      served at /favicon.ico (both apps)
src/test/java/...            JUnit 5 tests
esp32-poc/lib/solar_core/    portable C++17 core (no hardware): planner, daylight, control, wallbox, ocpp, history, setup, web/ApiHandler
esp32-poc/src/               hardware glue: platform (clock, NVS, LittleFS, diagnostics), net (Wi-Fi, setup network, mDNS, NTP, OTA),
                             enphase (client, poller), ocpp_server, web (WebServerAdapter), console (serial commands)
esp32-poc/test/              host tests (Unity): test_core, planner, daylight, control, api, persistence, history
esp32-poc/scripts/           copy_dashboard.py (pre-build), ota_auth.py (post-build, OTA env only)
docs/pdf-src/*.py            generate docs/*.pdf (deploy guide, test scenarios)
README.md, esp32-poc/README.md   user-facing docs (keep them current)
```

## Commands

Pi (Maven runs **offline**: the corporate mirror is unreachable):
```bash
mvn -o clean package                    # builds target/solar-charger-1.0-SNAPSHOT.jar and runs the tests
scp target/solar-charger-1.0-SNAPSHOT.jar wattsync@wattsyncpi.local:~/solar-charger.jar
sudo systemctl restart solar-charger    # on the Pi; a clean stop/restart saves history first
```
Run locally for UI work: `ENPHASE_TOKEN=local-test STATE_FILE=... HISTORY_FILE=... java -jar target/solar-charger-1.0-SNAPSHOT.jar`
(port 8080). Stop it with `pkill -f solar-charger-1.0-SNAPSHOT.jar` when done.

ESP32 (from `esp32-poc/`, using its `.venv`):
```bash
env -u CPLUS_INCLUDE_PATH .venv/bin/pio run -e esp32s3                  # build firmware (the env var breaks it on this Mac)
env -u CPLUS_INCLUDE_PATH .venv/bin/pio run -e esp32s3 -t upload        # flash by cable (UART/COM port, default env)
env -u CPLUS_INCLUDE_PATH .venv/bin/pio run -e esp32s3-ota -t upload    # flash over Wi-Fi (needs OTA firmware already on the board)
.venv/bin/pio device monitor -e esp32s3 --filter esp32_exception_decoder --filter time --filter log2file
CPLUS_INCLUDE_PATH=$(xcrun --show-sdk-path)/usr/include/c++/v1 .venv/bin/pio test -e native   # host tests
```
`pio device monitor` needs a real terminal (cannot be piped to tee). Close any monitor before uploading by cable.
The `esp32s3-usb` env is only for the board's native USB port; use the default env with the UART/COM port.

Dashboard tests: `/opt/homebrew/bin/node --test tests/dashboard/dashboard.test.mjs` (default `node` is v16 and fails).

Docs: `python3 docs/pdf-src/test_scenarios_pdf.py` and `python3 docs/pdf-src/deploy_pdf.py` regenerate the PDFs.

## Departure goal and messages

- `GoalPlanner` (Java `charging/`, C++ `planner/GoalPlanner.h`, same rules): force the lowest current that still reaches the goal when
  `secondsLeft <= remainingWh/(maxAmps*V)*3600 + 15 min`; energy counts from 16 h before the deadline (`carEnergyWhSince`). Hooked before the
  daylight check (Pi `EnphasePoller.goalTakesOver`, ESP `SolarController::goalTakesOver`; on the ESP the meters are still read for the
  dashboard via `goalForcing_` in `onMeterResult`). Ignored when paused, in Custom, or with no car (`carPluggedIn`).
- `Notifier` (Java `notify/`, C++ `notify/Notifier.*`): `HistoryService.setEventListener` feeds it every activity line; only the prefixes in
  `WORTH_SENDING` go out, same text at most once per 10 min; URL stored in state/NVS (`notifyUrl`), only the host is shown in telemetry
  (`notifyTo`). ESP sends from a short FreeRTOS task (`net/EspNotifySender`), https unchecked. Add a new event worth a message to BOTH lists.

## Control rules (Java `charging/` and C++ `planner/` must stay identical)

- Modes: **ECO** (min 6 A, starts at 7 A; the ONLY mode that subtracts the house load), **Max solar** (internal id MAX_SOLAR, the old
  BALANCED is still accepted by the API, the console and saved state files; shown as "Max solar"; min and start 6 A; input is the solar output alone, the house is ignored), **Custom** (internally FAST: fixed
  current 6-40 A, ignores the sun, the allowance, the daylight window and the fail-safe). Max 32 A for ECO and Max solar.
- ECO available amps = what the charger draws + (export + grid allowance) / voltage. Max solar uses solar watts / voltage and the allowance
  only tops up to the next whole amp or to 6 A when the gap fits (`topUpToWholeAmp` policy flag, `next(now, amps, topUpAmps)`). Voltage 240 on the ESP; the Pi defaults
  to 230 unless `HOUSEHOLD_VOLTAGE` is set. Two moving averages (slow 180 s for rises, fast 60 s for falls); the smaller is used.
- **Start**: smoothed available at or above the start level for **3 min** and at least **2 min** since the last stop.
- **Stop**: when smoothed available (ECO) or the solar input after top-up (Max solar) stays under the minimum for **5 min** (the charge runs at the minimum current meanwhile;
  the 5 minutes restart if the sun returns). This is a deliberate exception to "stay within the allowance": during a dip the
  car can draw its minimum from the grid. Custom ignores the allowance entirely.
- Limit changes: up needs 2 A and 60 s, down needs 1 A and 30 s. Meters are polled every 60 s.
- Grid allowance 0-1000 W (default 1000), a slider on the dashboard.
- **Daylight window**: ECO and Max solar only run from sunrise + 30 min to sunset - 60 min (NOAA formula, offline). Both margins
  are adjustable (0-180 min) in Settings. Outside it: no meter polling, a running charge is stopped once.
- **Fail-safe**: no Envoy data for 150 s stops ECO and Max solar. Custom and Paused still read the meters for display only.
- **Pause** is a kill switch that beats every mode: stops a charge, blocks starts and limit changes (also a car started from
  the wallbox's own app), survives restarts and crashes; Resume hands back to the saved mode.
- A charge found already running (restart, switch from Custom) is adopted, not stopped.

## HTTP API (identical on both apps; shared validation messages)

`GET /api/telemetry`, `POST /api/mode?mode=ECO|MAX_SOLAR|FAST[&amps=6-40]`, `POST /api/allowance?watts=0-1000`,
`POST /api/daylight?enabled=&startMin=0-180&endMin=0-180` (each optional), `POST /api/pause?enabled=`, `POST /api/rate?cents=0-200[&peakCents=&peakStart=0-23&peakEnd=1-24]`, `POST /api/maxamps?amps=6-40`,
`POST /api/goal?kwh=0-100[&byMinutes=0-1439]`, `POST /api/notify?url=` and `POST /api/notify/test`,
`GET /api/history[?daysAgo=0-364]` (5-min samples, totals, daylight window; no events), `GET /api/days?n=1-365`,
`GET /api/events?n=1-20&offset=0-1000` (paged, with total), `GET /api/sessions?n=1-100&offset=` (paged, with total and the open one),
`GET /api/diagnostics`, `GET /api/export.csv`, `GET /favicon.ico`. Bad input gets a 400 and changes nothing.
**Every POST needs the header `X-WattSync: 1`** (cross-site protection; 403 without). The dashboard's `post()` adds it; use
`curl -H 'X-WattSync: 1'`. When adding a POST route on the ESP, also register it in `esp32-poc/src/web/WebServerAdapter.cpp` (maxamps was
once forgotten there).
The portable handler is `esp32-poc/lib/solar_core/src/web/ApiHandler.*`; the Pi's is `web/ApiRoutes.java`.

## Dashboard (`src/main/resources/dashboard.html`)

One file, mobile-first, no build step. The ESP build copies it (and `favicon.png`) into `esp32-poc/src/web/` (git-ignored).
After editing it, rebuild **both** the jar and the firmware, and `cmp` the two copies.
- Layout: phone = one column (headline, flow, numbers, modes, graph, activity, sessions). Wide screens (>=900 px) = two stacks
  on top (headline/flow/numbers | modes/activity) and graph beside sessions below, all ending level; the graph grows to fit.
- Gear icon (top right) opens the Settings sheet: solar hours and start/stop minutes always shown; Charge by morning, Prices, Limits and Messages are collapsed optional groups (`details.opt`); then a collapsed Diagnostics list.
  The pill says Live, or turns amber "Meter 12m old" when a daytime reading is stale.
- Blue "i" buttons open plain-words help cards (`TIPS` map). Activity and Charging sessions show 5 per page and fetch only that page.
- Sliders (Custom current and Grid allowance) send once, 5 s after you stop moving, with a countdown; the outcome fades after 4 s.
- Elapsed times are short: `30s`, `45m`, `~5h`, `~2d` (half-hour rounds down, more rounds up). At night the Solar tile says "Resting until sunrise".
- Wording rule: plain, short, no jargon. Number boxes use 16 px text (smaller makes iOS zoom the page).
- Pull-to-refresh (a text-free WattSync-logo indicator, `#ptr`: logo grows and arrows turn with the pull, bolt pops when ready, arrows spin while loading; driven by touch handlers near the end of the script) runs only in standalone mode (Home Screen app) or with `?pull`; it reloads the page. Keep it at the top level of the script, next to the final `refresh(); refreshHistory();` (an earlier edit once landed inside `sendMargins`).
- `?debug` on the URL shows a small layout/zoom readout. Real-device zoom problems were a remembered Safari zoom, not the layout.

## History, settings and persistence

- Tiers: 1-min samples 48 h, 5-min records 30 days, hourly 1 year, daily totals 400 days, events 50, sessions 400.
- ESP: settings in NVS; history in LittleFS: `history.tsv` (full save every 30 min if changed), `history-long.tsv` (append-only,
  compacted when stale), `history-journal.tsv` (rewritten every minute with only what is new since the last full save, replayed
  and folded in at boot). Long tiers live in PSRAM `Ring<T>` buffers (`bigAllocator`). A low-memory guard returns 503 for `/api/history`.
- Pi: settings in a JSON state file; history in `HISTORY_FILE` (+ `.long`, `.journal`), full save every 30 min and on clean stop,
  journal every minute. A crash or power cut loses at most about a minute.
- Savings = the sun's share of the car's energy times the price (cents/kWh, default 30). Sessions come from OCPP Start/StopTransaction.
- ESP diagnostics (NVS `diag`): restarts, crashes (panic, watchdog, brownout), last crash with time. Boot banner prints `Last restart`;
  a `Memory:` line prints every minute. Serial logs are NOT stored on the board.
- OTA: ArduinoOTA, password `OTA_PASSWORD` in `esp32-poc/src/secrets.h` (git-ignored; also holds Wi-Fi, Enphase token, Envoy IP).
  **Never print secrets.** espota's `--debug` echoes the password; `scripts/ota_auth.py` strips it. The board needs one cable flash
  of OTA-capable firmware first. No automatic roll-back.

## Setup network (ESP32 only)

The board can open its own Wi-Fi `WattSync-Setup` (password `wattsync`, fixed for now) with a page at `http://192.168.4.1/setup` (`esp32-poc/src/web/setup.html`,
embedded like the dashboard; it is a tracked file, not copied from the Java app). From a phone: pick the home Wi-Fi from a scanned list, paste the Enphase token,
the Envoy address and the update (OTA) password. Wi-Fi name and password, token, Envoy address and OTA password are no longer compile-time only: they live in NVS
namespace `net` (`NvsCredentialStore`) and **win over `secrets.h` field by field** (`solar::setup::effective`); an Envoy address saved as `"auto"` means "find it as envoy.local".
- Logic (portable, host-tested in `test/test_setup`): `lib/solar_core/src/setup/SetupLogic` (form validation, token cleaning, IPv4 check, `tidyScan`, the Wi-Fi trial) and
  `SetupApi` (`/api/setup/status|networks|scan|save`, 403 while setup is closed or without `X-WattSync`; replies never contain a secret; secrets arrive in a POST body).
- Hardware glue: `src/net/SetupPortal` (AP+STA, DNS catch-all, async scan, 15-minute no-use limit, BOOT held 5 s on GPIO0, restart after a save), start-up flow in
  `main.cpp` `bringUpNetwork()` (the web server now starts BEFORE Wi-Fi so the page can be served), routes and captive-portal redirects in `WebServerAdapter`.
- Opens: no Wi-Fi set (never closes), joining fails 3 min at start-up (once per start), BOOT held, or console `setup`. Console also has `wifi` and `setup clear`.
- A changed Wi-Fi is a **trial**: saved with the previous one remembered, tried at the next start; if it cannot join, the previous is put back and the reason is shown on the
  page (`TrialState.lastError`). Never drop saved credentials just because a join failed. With no OTA password anywhere, OTA stays off.
- Known gaps: password is the same on every board (planned: derive from the chip ID); nothing opens by itself if Wi-Fi is lost while running.

## Conventions and preferences

- Keep `README.md`, `esp32-poc/README.md`, the PDFs in `docs/` and the memory notes updated whenever a feature or rule changes,
  without being asked, and mention it briefly in the summary.
- When the user says "tell me before making the change", propose it (wording, a mockup) and wait. Otherwise implement.
- Verify UI changes in the browser pane at about 402 px, 375 px, 320 px and several desktop widths (930, 1280, 1600), then reset
  the viewport to desktop. The pane sometimes closes; reopen it with `navigate`.
- Never include personal data in replies. Separate verified facts from assumptions; say what was not tested (the board and the
  real wallbox are often untested for the latest change; nothing is deployed until the user does it).
- Tests: the planner and control tests encode the timing rules above; update them with the rules. Java and C++ share the same numbers.

## Gotchas

- BSD `sed` on macOS needs `-i ''`. `register` and Unity double precision are off in the host tests (use `TEST_ASSERT_FLOAT_WITHIN`).
- The ESP web server runs in the main loop and logs "request handler not found" for unregistered routes: register every new
  route in `WebServerAdapter.cpp` explicitly.
- `.local` names can take about 5 s to resolve on a Mac (the board answers IPv4 only); the board itself answers in about 30 ms.
  Using the IP (and a router DHCP reservation) avoids it.
- `main.cpp` is touched on every build so the firmware-built time in Diagnostics is fresh.
- A new embedded page must be added to `board_build.embed_txtfiles` in `platformio.ini` (symbol `_binary_src_web_<name>_html_start`).
- `WiFi.mode` must be set (and the hostname set before it) before `WebServer::begin()`; the setup page needs the server up with no Wi-Fi.
- Test output from `mvn` prints WARN lines (fail-safe tests); they are normal.

## Not yet verified on real hardware

The whole setup network on the board (flow, the iPhone setup sheet, scanning while a phone is joined, the trial and undo of a bad password, BOOT hold), the board's start-up with no internet, the safe-header collection (`collectHeaders`), the new NVS keys, notifications over https, the departure goal on a real charge, the PSRAM allocator and flash journal at scale, the 3 min / 5 min timing on a real charge, the board's OTA update flow end to end
after the first flash, and an overnight run watching for crashes. Ideas not built: a native iOS wrapper, a separate total-grid-import cap (Eco's allowance does the same job),
gzip for the page, a hint card for Add to Home Screen, a minimum run time instead of the stop delay.
