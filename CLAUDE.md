# WattSync (folder and jar still named SmartEnergyApp)

A solar EV-charging controller. It reads the house's solar, home and grid power from an Enphase Envoy gateway,
decides how fast a wallbox should charge the car, and talks to the wallbox over OCPP 1.6J. Two implementations
share one dashboard page. Only one of them can be connected to the wallbox at a time.

- **Pi**: Java 17 / Javalin app (this folder). Host `wattsyncpi`, dashboard and OCPP both on port 8080.
- **ESP32-S3**: C++ firmware in `esp32-poc/` (PlatformIO, Arduino). Host `wattsync`, dashboard on port 80,
  OCPP on port 8080: owners enter `ws://wattsync.local:8080/pair` (the wallbox appends its id; `/ocpp/wallbox/<id>` still works), OTA on 3232. Board: ESP32-S3 N16R8 (16 MB flash, 8 MB octal PSRAM).
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
docs/pdf-src/*.py            generate docs/*.pdf (deploy guide, test scenarios, user guide)
docs/demo-src/make_demo.py   screenshots (docs/guide) and demo GIF (docs/demo) from the real dashboard on sample data
README.md, esp32-poc/README.md   user-facing docs (keep them current)
docs/README.md               index of everything under docs/
docs/setup-network.md        the ESP32 setup network: diagrams, UI mocks, endpoints, safety (start here for that feature)
docs/diagrams/*.svg          drawn by docs/diagram-src/make_diagrams.py (edit the script, not the SVGs)
docs/ui/*.jpg                screenshots of the setup page at 402 px, one per state
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

Docs: `python3 docs/pdf-src/test_scenarios_pdf.py` and `python3 docs/pdf-src/deploy_pdf.py` regenerate the PDFs;
`python3 docs/diagram-src/make_diagrams.py` regenerates the SVG diagrams.

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
- **Cheapest hours** (id CHEAPEST, `cheap` accepted; branch `cheapest-hours`, decided 2026-10-31): for time-of-use plans, with or without solar. Charges at `maxAmps` outside the expensive hours, stops inside them. The expensive hours ARE the existing peak window (`peakStartHour`/`peakEndHour`, used even when `peakRateCents` is 0) plus `peakWeekends` (default true; false = Mon-Fri only; `POST /api/rate?peakWeekends=`). Seasons deliberately not modelled: the California plans checked (PG&E E-TOU-C, SCE TOU-D-4-9PM; summer Jun-Sep, winter Oct-May) keep the same 4-9 pm window all year, only prices change. Pure rule in Java `charging/CheapHours` and C++ `planner/CheapHours.h` (must stay identical). Pi: `EnphasePoller.applyCheapHours` each poll + `WallboxController.onChargerChanged` + `ApiRoutes.applyCheapNow`; ESP: `SolarController::applyCheapHours` through `applyHold` (the wallbox hold). Goal planner still wins; no fail-safe, no daylight window, meters read for display only. Telemetry `controller.expensiveNow`, `controller.secondsToPriceChange` (no `waitSeconds`). History mode code 3. NVS key `peakWknd`.
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
  are adjustable (0-180 min) in Settings. Outside it: no meter polling (a deliberate rule, confirmed by the owner 2026-11-01: do NOT read the meters after solar hours), a running charge is stopped once. So the page must not show old numbers as live: `flowUnmeasured(data)` (Eco or Max solar, window closed, reading older than 3 polls) makes the flow show `–` / `Not read at night` for Home and Grid, and `unmeasuredNote(...)` puts `Last reading at 8:22 pm. Eco and Max solar do not read the meter after solar hours.` under the Today graph instead of a stale Now line. A patch that made the controllers read for display at night was tried and dropped (not wanted).
- **Fail-safe**: no Envoy data for 150 s stops ECO and Max solar. Custom and Paused still read the meters for display only.
- **Pause** is a kill switch that beats every mode: stops a charge, blocks starts and limit changes (also a car started from
  the wallbox's own app), survives restarts and crashes; Resume hands back to the saved mode.
- A charge found already running (restart, switch from Custom) is adopted, not stopped.

## HTTP API (identical on both apps; shared validation messages)

`GET /api/telemetry`, `POST /api/mode?mode=ECO|MAX_SOLAR|FAST|CHEAPEST[&amps=6-40]`, `POST /api/allowance?watts=0-1000`,
`POST /api/daylight?enabled=&startMin=0-180&endMin=0-180` (each optional), `POST /api/pause?enabled=`, `POST /api/rate?cents=0-200[&peakCents=&peakStart=0-23&peakEnd=1-24&peakWeekends=true|false]`, `POST /api/maxamps?amps=6-40`,
`POST /api/goal?kwh=0-100[&byMinutes=0-1439]`, `POST /api/notify?url=` and `POST /api/notify/test`,
`POST /api/car/mode?enabled=&confirm=yes` (Tesla mode), `GET /api/car/hotspot`, `POST /api/clock?epoch=`, `POST /api/car/pair` (VIN in the BODY, never the URL) `|refresh|watch?enabled=|test?action=start|stop|amps[&amps=6-40]|through?mode=charger|car[&confirm=yes]|policy?onReturn=keep|tell|stop|cancel|forget` (ESP32 only; the Pi answers 400 "needs the ESP32 board"; telemetry `car`),
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
- Layout: phone = one column (headline, flow, value and graph card, modes, activity, sessions; the order is set by `order:` on the ids). Wide screens (>=900 px): the headline across the top, then the savings and graph card on the left (it spans the flow, modes and Tesla grid rows) level with the power flow and the modes on the right, then activity and sessions below (grid placement by id on `.dash`, `.stack` is `display: contents`; tile bottom margins instead of row gaps so a hidden Tesla tile leaves no gap). The flow row is `minmax(0, 1fr)` and the flow card's two arrow rows are `minmax(40px, 1fr)`, so any height difference is taken up by longer arrows, never by empty space inside a card. Aim: the card ends inside an 800 px screen at 1280 px wide. Check wide screens with real-length lists (five wrapped rows each), not empty ones: empty lists hid this once.
- Gear icon (top right) opens the Settings sheet: ONE scrolling page of cards (`section.sc`: Solar hours, Ready by, Limits, Money, Messages, Tesla `#sec-tesla`, Display + a collapsed Diagnostics) under a sticky jump bar (`#set-nav`, chips Charging/Money/Messages/Tesla/More scroll to `#sec-*`, the one in view is highlighted); two columns from 900 px. All checkboxes are drawn as switches (`.check`). **Ready by** (`#goal-toggle`, `goalSwitch`) and **Higher price at certain hours** (`#peak-toggle`, `peakSwitch`) are switches over optional boxes (`#goal-fields`, `#peak-fields`): on = show the boxes prefilled (last goal or 10 kWh; last peak price or 1.5 x the price) and wait for the check mark (`goalPending`/`peakPending`); off = send goal 0 / peak price 0 at once. The server state is still just `goal.kwh > 0` and `peakRateCents > 0` (API unchanged).
  The pill says Live, or turns amber "Meter 12m old" when a daytime reading is stale.
- **Theme** (Settings > Display > Theme: Match my phone (default) / Light / Dark): all colours are CSS variables in `:root` (light), `:root[data-theme="dark"]` and `@media (prefers-color-scheme: dark) { :root:not([data-theme="light"]) }` (the last two must stay identical; a test checks it and that every light variable has a dark value). Never add a literal colour outside those blocks (a test greps the old ones); fills with white text use `--blue`/`--green-fill` (same in both themes), accent text uses `--link`/`--green`/`--red`. The chart, bars and flow arrows are drawn in script and read colours with `cv('--name')`; `applyTheme()` redraws them. The choice is `localStorage 'theme'` (only `light`/`dark` stored); `<script id="theme-boot">` in the head applies a forced theme before first paint (it must keep its `id`: the tests match the main script by a bare `<script>`). Not themed: `setup.html`.
- Units: Settings > Units shows energy as kWh or estimated miles (`~`, kWh x miles-per-kWh, default 3, 1-6). Display only, stored in the phone's `localStorage` (`unit`, `mpk`); API, planner, savings, CSV and message text stay kWh. Helpers `fmtEnergy`, `fmtCarPower` (big number on the Car tile) and `fmtCarPowerOther` (the line under it: the other unit); when the draw is only the set limit (`drawMeasured` false) both get a `~` via `fmtApprox` (miles already have one, zero stays plain), `goalToBox`/`goalFromBox` (a goal above 0 is never rounded to 0 kWh); `applyUnits()` redraws. Event text from the controller still says kWh.
- Tesla (ESP32 only): Settings > Tesla group (`#car-group`: VIN + Pair, live pairing box, watch switch, Check now, Forget, collapsed For testing with Start/Stop/amps and "put back"); a `#carcard` tile under the modes when a car is paired and `car.supported`. `carView(c, charger)` and `carProgressText(c)` are pure and tested. Test buttons always `confirm()` first. The VIN goes through `postForm` (body), the box is cleared after sending. Hidden `.field` rows need `.field[hidden]{display:none}`.
- Solar meter notices (vendor-neutral wording: never name a make, other meters will follow): telemetry `meterLinked` (false when no token/address is set on the board; the Pi is always true) and `enphaseOk`. `meterView(data)` is pure and tested: never set up = grey bar "No solar meter paired", set up but unreadable = red bar "Unable to read solar meter" (grey in Tesla mode, which points to Custom). Eco and Max solar are greyed out (help line "Needs the solar meter. Use Custom.") when never linked, or once a linked meter has been unreadable for over 150 s (2.5 polls; Custom never trips the fail-safe, so the fail-safe flag alone is not enough) or the fail-safe has stopped charging; Custom is never locked. Diagnostics > Solar meter row uses the same words.
- Cheapest hours UI: mode buttons are a 2 by 2 grid (`--cx`/`--cy` move the highlight); `#cheap-panel` under them (`priceView(data)`, pure and tested: price now, next change, the "Charges at up to N A, except 4:00 pm – 9:00 pm every day." line; `changeCheapHours()` opens Settings > Money with the hours boxes showing; `#peak-weekends` switch posts `peakWeekends`). `flatMode(data)` = Custom or Cheapest (no solar hours, no meter notice). With `meterLinked` false, `renderNoSolar` hides the power flow and the solar-share bar and shows `#nosolar` tiles (Price now, Cheap at / Peak at, Car); the Pi always has a meter so it never shows them. `waitView` says "Waiting for cheaper hours" with the start time.
- Location (branch `site-location`, 2026-10-31): `solar::Site`/`SiteControl` (`site/Site.h`, portable: `timezones::posixFor` maps an IANA name to a POSIX clock rule for the US and Canadian zones, unknown zone = 400; `parseSite` validates and rounds to 2 decimals), board `device::SiteSettings` (NVS namespace `site`, read first in `setup()` -> `daylight.setLocation`, `siteClockRule` -> `TimeSync::begin(gateway, rule)`; `save()` restarts the board after 1.5 s like Tesla mode; shared by the charger and Tesla setups), `POST /api/site` (body: lat, lon, tz, name; registered in `WebServerAdapter`), telemetry `site {lat, lon (0.1 deg), set, timezone, name}`. Pi: `config/SiteInput` (same messages), `SystemState.setSite`, saved in the state file (`PersistedState` siteLat/siteLon/siteTz/siteName), `AppConfig.useSite(...)` at start-up beats the env vars and the default; applied after a restart of the app. Dashboard: `#site-chip` ("Set your location" while `site.set` is false), Settings > Solar hours `#site-box`, lookup by the PHONE at `geocoding-api.open-meteo.com` (`siteLookupUrl`, `siteChoices`, `siteSummary`, `siteChipWanted`, `findSite`, `useSite`; location goes in the BODY via `postForm`; zip code never stored). The forecast needs `site.set`. Hidden elements whose CSS sets `display` need their own `[hidden]` rule (`.impchip`, `#site-box select`).
- Tomorrow's forecast (branch `forecast`, idea 4; phone-side only): bubbles `#td-chip` (Today) and `#fc-chip` (Tomorrow) next to the score chip open `#fcbox` (2026-11-02: the whole chips row now sits in its own full-width row INSIDE the headline card, below the text and Pause, `.hero` wraps and `.hero .chips` is `flex: 0 0 100%`, hidden by `:has()` when no bubble shows; each bubble is a weather picture `WX_ICONS[kind]` (`weatherKind(cloudPct, rainMm)`: sun / partly / cloud / rain; the forecast URL also asks for hourly `precipitation`) plus `forecastChipText` "Tomorrow ~30 mi ›" / `todayChipText` "Today ~93 mi ›", "Today ~78 mi made ›" after the solar hours; `todayView(days, ratios, todayNum, madeKwh, solarHoursOpen)`: whole-day estimate = learned ratio x today's model sunshine, never below what is made, after the solar hours the measured total from `hist.totals.solarKwh`; `todayLine` adds it to the sheet as `#fc-today`; the condition WORDS were dropped from the bubbles) which open `#fcbox` (same dialog styles as `#sharebox`). The PHONE fetches `https://api.open-meteo.com/v1/forecast` (hourly `shortwave_radiation,cloud_cover`, `past_days=7&forecast_days=2&timezone=auto`; CORS is open) using telemetry `site.lat/lon` (the controller rounds them to 0.1 deg; the exact site never leaves it), at most every 3 h (`FORECAST_EVERY_MS`, retry 15 min), cached in `localStorage 'fcache'` (reused only while younger than 3 h AND it still includes tomorrow, `forecastCovers`: after midnight an evening answer lacks the new tomorrow, which once hid the button for up to 3 h; `ensureForecast` refetches when the day rolls); switch `localStorage 'forecast'` (default on, Settings > Display). Pure and tested: `forecastUrl`, `forecastByDay`, `forecastView(f, daysReply, todayNum, mode)` (label from daylight cloud cover, best window = hours at 50 percent of the peak, kWh = median over the last 7 days of actual solar / model kWh-per-m2 times tomorrow's model figure, needs 3 days), `forecastLines`, `forecastWanted` (needs `meterLinked` and `site`). Free tier is NON-COMMERCIAL only (paid plan if WattSync is ever sold or has ads); credit link is in the card. No control logic uses it. The button also shows the expected solar in the owner's unit (`forecastChipText`, `forecastLines(v, miles, mpk)`, `forecastAmount`; miles = kWh x miles per kWh with a note that the house takes its share). TODO (owner's request): move all of these calculations to the backend (see the memory note on the re-architecture); today they run only in the phone's browser.
- Score wheel (Impact card and the headline button): `.scorering` (NEVER plain `.ring`: the headline card gets the class `ring` while it counts down, and a bare `.ring` rule once turned the whole card into a grey oval on a phone; tests `the score wheel never styles the headline card` and `every class the page toggles on the headline card is unique to it` guard this) is a CSS conic-gradient (`var(--solar)` to `var(--green)` at `--p` x 3.6 deg, then `var(--soft)`) cut to a ring with a radial mask (`--t` thickness); `--p` is a registered number (`@property --p`) so it can transition, only inside the no-preference motion block; `gradeColor(score)` gives the letter colour (`color-mix`), `wheelLabel(view)` the screen-reader text; `openImpact()` restarts the fill from 0 when motion is allowed. Elements whose CSS sets `display` need their own `[hidden]` rule.
- Your impact tile (`#impact`, branch `impact-score`; phone-side only, no API change). All widths: a chip `#imp-chip` under the headline (`impactChipText`: "Solar Score 74 ›", the grade is only the letter in its small ring) opens `#impact` as a bottom sheet on phones and a centred two-column card from 900 px (class `open`, backdrop `#impact-back`, Done `#imp-done`, Esc and the Tab trap handled with the other sheets; it was an inline tile at the bottom of the page first, which nobody found): `impactView(ctx)` (pure, tested) builds a Solar Score / Charge Score (`impactScore`: solar share 50 + off-peak share 30, a part that does not apply is dropped and the rest rescaled; grades A 90 / B 75 / C 60 / D), a streak (`streakOf`; sunny-day from `/api/days` rows, peak-free from sessions), badges (`ladderStep`), planet and wallet numbers (`impactNumbers`: 8.9 kg CO2 a gallon, 0.23 kg per kWh grid, both rough) and the share card words (`shareCardLines`; `shareImpact()` draws a canvas and uses the share sheet). Off-peak share comes from `GET /api/sessions?n=100` split by site hour (`siteMoment` from `hist.dayStart`/`hist.today`, `perDayPeak`, same rule as `CheapHours`), assuming even draw. No solar = `meterLinked === false` or under 0.5 kWh solar in the period: no CO2 line, no sunny streak. Streaks look back only as far as the 30 days loaded. Goals: score part (20) and the `ready` badge come from `/api/days` rows' 8th element (0 none, 1 met, 2 missed). **Saved by Cheapest hours** = `cheapSavings` (sessions in CHEAPEST with `plugged` known: energy that would have fallen in the expensive hours if charged at max amps from plug-in, minus what did, times peak minus normal price). Plan picker: `PLANS`/`planFor`/`applyPlan` in Settings > Money (hours and weekends only; only the two plans checked in Oct 2026). Share image: the Web Share API needs https, the board serves http, so `showShareCard` shows the picture to press and hold (and a Save button) instead of downloading.
- Waiting headline (`waitView(data)`, pure and tested): when Eco/Max solar is only waiting, the title is the reason ("Waiting: home is using the solar", "Waiting for more solar", "Starting soon", "Waiting to restart", "Waiting for sunrise") and the line under it says what is needed or what will happen, with a countdown, in words that need no background ("Needs 1.1 kW of solar left over after the house.", "Starts in about 2 min if solar stays this strong.", "Stops in about 3 min if solar stays low."; the solar and house numbers are NOT repeated, the power picture right below has them; a test bans "if it holds" and "spare to start"); a charge riding out a dip says "Low solar, charging at N A" (amber). Countdowns use telemetry `controller.waitSeconds` (-1 = no timer): the planner's `Decision.waitSeconds` (start confirmation 3 min / pause after a stop 2 min / dip 5 min) kept in `SystemState.Control` and reduced by the reading's age at request time. Java and C++ planners must stay identical. Wording says "solar", never "sun".
- Bar hover (month/week/year bars, `drawBars`): a MOUSE (`pointerType === 'mouse'`) hovering a bar highlights it (`lit` rect) and writes `barReadoutText(row, unitMiles)` into `#readout` (date, total, sun and grid split, saving; rows carry `date`); leaving restores `barHint()` (`canHover()` picks the hover or the tap wording); a finger still just taps through to that day. Touch has no hover, by design.
- Day graph (`drawChart`): grid power is drawn as red bars standing up from the zero line (import, `--grid`) and green bars hanging down (export, `--green`); `exportFloor(minGridWatts, ymax)` (pure, tested) gives the room below zero in 500 W steps, only on days with export, at most half the top side; `chartSpan(h)` (pure, tested) starts the time axis 5 min before the day's first active reading (`chartActive`: solar >= 50 W, car >= 50 W or the charging flag; quiet overnight readings are not drawn; no fixed 6 am) and ends where the daylight window ends (or just past a later reading), so the shaded solar hours reach both edges. The graph box is 250 px tall on phones; the summary is a 2x2 grid (`.summary.cols`) and the "Now" reading labelled columns (`#readout.rd`) so neither wraps unpredictably. Home is violet (`--homeline`); idle flow arrows and charging-strip stop marks use `--stop`, the zero line `--zero` (dark palette greys were near-invisible). Always visible: `#range-summary` (Solar, Car, Imported, Exported from `hist.totals`) above the graph and the "Now" reading in `#readout` below it; hover shows a moment, a finger tap keeps its reading (`pointerleave` only resets for a mouse).
- Motion (all CSS animations sit in one `@media (prefers-reduced-motion: no-preference)` block, a test checks it; script side is gated by `motionOk()`): flow dots are a second dashed SVG line per live arrow, moved by a `requestAnimationFrame` loop (`flowTick`, phase kept in `flowPhase` across `drawFlow` rebuilds, stops while hidden, speed from watts); numbers use `glide(id, value, fmt, cls)`; hero title slides (`.swap`), badge `.pop` on start, while waiting, with no countdown a blue arc circles the icon (`wspin`, animated `@property --a`; no swell, it would fight the arc), `.ring` with `--wp` from `controller.waitSeconds` (`waitTotal`); `#seg` highlight is `.seg.hl::before` as a real grid item (do NOT call it `pill`: `.pill::before` is the Live dot); slider bars `#fast-cd`/`#allowance-cd` via `setCd`; new rows get `.fresh`.
- Value + graph card (`#chartcard`; `#value` is its top part, replacing the three number tiles and the old graph tabs): Today / Week / Month / Year tabs (`#tab-day|7|30|365`, `range` = `day|7|30|365`, `setRange`; `showMode` highlights them and re-renders the numbers), Energy or Range added, Saved, a solar-share bar, a unit flip button (`flipUnit`, the same `unit` setting as Settings; its label names the unit it switches TO: Show miles / Show kWh; on phones up to 400 px it drops to its own row under the tabs), then the graph in `#graphpanel`. On wide screens the card is a grid (three number boxes in a row, the saving line, the Graph / Compared with gas tabs `#gtabs`, then one cell holding both `#graphpanel` and `#gaspanel`, the inactive one `visibility: hidden` so the card never changes height; `setGasView`, class `gas-view`; the saving line also switches). On a phone the panel is folded (`.gclosed` on `#chartcard`, `toggleGraph`, `localStorage 'graphopen'`, redraw on open because a hidden chart has no size); from 900 px it is always shown and the toggle is hidden. The bar views hide `#range-summary` (the numbers above are the totals); the day curve keeps its own. `valueSummary(kind, days, totals)` is pure and tested: rows come from `/api/days` (30 days fetched at start, 365 only when Year is picked, refreshed at most every 5 min; `ensureValueDays`), today's row is replaced by the live `hist.totals`, Year is the twelve months ending this month (same window as the graph bars), the sun's share never exceeds the car's energy, a day with no recorded saving is estimated at the normal price. Gas comparison (`#vgas` line + `#gaspanel`, phone-only settings `gasPrice`/`gasMpg` in `localStorage 'gasprice'|'gasmpg'`, defaults 4.60 and 28 = guesses; the open state is `'gasopen'`): `gasCompare(carKwh, saved, rateCents, mpk, mpg, price)` is pure and tested (miles = kWh x miles per kWh; grid-only = kWh x normal price; solar = the card's Saved, capped at grid-only; paid = grid-only - solar; "less than gas" = gas cost - paid, and the line says "more" if it comes out negative). Miles per kWh is always visible in Settings now. A finished charge shows `#sesscard` under the headline for 20 s (`checkSessionDone` in `loadSessions`, text from `sessionDoneText`); the first sessions load is only the baseline.
- Blue "i" buttons open plain-words help cards (`TIPS` map). Activity and Charging sessions show 5 per page and fetch only that page.
- Sliders (Custom current and Grid allowance) send once, 5 s after you stop moving, with a countdown; the outcome fades after 4 s.
- Elapsed times are short: `30s`, `45m`, `~5h`, `~2d` (half-hour rounds down, more rounds up). At night the Solar tile says "Resting until sunrise".
- Wording rule: plain, short, no jargon. Number boxes use 16 px text (smaller makes iOS zoom the page).
- Pull-to-refresh (a text-free WattSync-logo indicator, `#ptr`: logo grows and arrows turn with the pull, bolt pops when ready, arrows spin while loading; driven by touch handlers near the end of the script) runs only in standalone mode (Home Screen app) or with `?pull`; it reloads the page. Keep it at the top level of the script, next to the final `refresh(); refreshHistory();` (an earlier edit once landed inside `sendMargins`).
- `?debug` on the URL shows a small layout/zoom readout. Real-device zoom problems were a remembered Safari zoom, not the layout.

## History, settings and persistence

- Goal outcomes and plug-in times (2026-10-31): `HistoryService.recordGoalResult` (once per deadline, from `GoalTracker`: Java `charging/GoalTracker`, C++ `planner/GoalTracker.h`, called at the start of each poll/tick; not judged when paused or in Custom, when the deadline passed more than 15 min ago, or with no car and no energy) is saved as its own history line `G\t<day>\t<1 met|2 missed>\t<target Wh>\t<done Wh>` (older versions skip it), per-day, 400 days; `/api/days` rows end with it. Sessions carry `pluggedAt` (a trailing column after `mixed`; set by `notePluggedIn()` on the Available-to-Preparing status change, dropped if over 3 days old or not seen), exposed as `plugged` in `/api/sessions`. Not kept across a restart between plug-in and session start. Sessions also carry `tag` (trailing column after `pluggedAt`; branch `idtag-hash`, a spike for household sharing): the first 8 hex digits of SHA-256("wattsync:" + the StartTransaction `idTag`), Java `ocpp/IdTagHash`, C++ `ocpp/IdTagHash.h` + `common/Sha256.h` (same test vectors). **Only the fingerprint is stored, logged or served, never the raw tag.** Note WattSync's own remote starts send a random tag each time, so those sessions all differ; only charges started from the wallbox's own app or card show a stable fingerprint.
- Tiers: 1-min samples 48 h, 5-min records 30 days, hourly 1 year, daily totals 400 days, events 50 and no older than 24 h, sessions 400 and no older than 30 days (2026-11-09; `HistoryService.pruneOld` once a minute and when a line is added, plus a read-time cutoff so an old line never shows; Java and C++ identical; the graph's tiers and the daily totals are separate and untouched, a test proves it; nothing is purged while the clock is not set). **Limit changes are merged** (`HistoryService.addEvent`, Java and C++ identical): "Changed limit to 9 A: less solar left" (or the car's "Car: changed limit to ...") followed by more of the same kind less than an hour apart with nothing else logged between becomes one line "Changed limit 4 times, now 13 A: more solar" stamped with the latest change; the Notifier still hears every raw line. The same fold runs when saved lines are loaded (`mergeLimit`), so lines saved before the merge existed collapse at the next start, and a journal that overlaps a full save replaces the older snapshot of a run instead of doubling it.
- ESP: settings in NVS; history in LittleFS: `history.tsv` (full save every 30 min if changed), `history-long.tsv` (append-only,
  compacted when stale), `history-journal.tsv` (rewritten every minute with only what is new since the last full save, replayed
  and folded in at boot). Long tiers live in PSRAM `Ring<T>` buffers (`bigAllocator`). A low-memory guard returns 503 for `/api/history`.
- Pi: settings in a JSON state file; history in `HISTORY_FILE` (+ `.long`, `.journal`), full save every 30 min and on clean stop,
  journal every minute. A crash or power cut loses at most about a minute.
- Savings = the sun's share of the car's energy times the price (cents/kWh, default 30). Sessions come from OCPP Start/StopTransaction.
- ESP diagnostics (NVS `diag`): restarts, crashes (panic, watchdog, brownout), last crash with time. Boot banner prints `Last restart`;
  a `Memory:` line prints every minute. Serial logs are NOT stored on the board. `solar::MemoryWatch` (portable, `common/MemoryWatch.h`; `EspDiagnostics::service()` every 5 s from `loop()`) keeps 24 h of 5-minute memory samples in PSRAM and the lowest free heap with when; `GET /api/diagnostics` shows `memory.psramFreeKb`, `memory.lowest`, and `?memory=1` adds `memorySamples` (written straight into the text). First readings (2026-10-31): internal heap about 148 KB free, lowest 130 KB reached about 35 s after boot, PSRAM about 7.9 of 8 MB free. Spinning arc on the hero icon is only for short waits (`rest` flag on night and expensive-hours waits).
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
- **Wi-Fi lost while running** (found 2026-10-31: the board stayed offline after the router came back): `WifiConnection::keepAlive()` in `loop()` follows `solar::WifiWatchdog` (pure, host-tested in `test/test_wifi_watchdog`: 15 s grace, reconnect, fresh join every 60 s, radio reset every 5th; never restarts the board). Skipped in Tesla mode and while the setup network is open. Flashed, but the real router-off test has not been run.
- A changed Wi-Fi is a **trial**: saved with the previous one remembered, tried at the next start; if it cannot join, the previous is put back and the reason is shown on the
  page (`TrialState.lastError`). Never drop saved credentials just because a join failed. With no OTA password anywhere, OTA stays off.
- Known gaps: password is the same on every board (planned: derive from the chip ID); nothing opens by itself if Wi-Fi is lost while running.
- Full write-up with diagrams, the eight UI mocks, the form rules, endpoints and code map: `docs/setup-network.md`. Keep it, the diagrams (edit
  `make_diagrams.py`) and the screenshots in `docs/ui/` current when this feature changes. Screenshots come from the real page served by a stand-in
  for the board's API (no board needed): serve `setup.html` plus fake `/api/setup/*` replies, resize the browser pane to 402 px and capture each state.

## Tesla car control (pairing, status and charge commands proven on a 2021 Model Y; Control through: Car and wake built, untried on a car)

Decisions (2026-10-26): works for other Tesla owners, so each controller makes its own key and nothing secret ships in the repo; **one commander at a time**,
the user picks **Control through: Charger | Car** (user-facing word is "charger", code still says wallbox/charger for the OCPP hardware); runs on both the ESP32 and the Pi;
unreachable car = ramp to the minimum current first, keep the last setting, retry with backoff, message after 10 min, owner chooses keep / just tell me / stop on return.
Built so far (ESP32 console only): `tesla scan [seconds]` / `tesla scan <VIN>` (range, never connects), and since 2026-10-27 `tesla pair <VIN>` (adds our own key with the Charging Manager
role; the owner taps their key card), read-only `tesla status` (never wakes the car, sends no charge command), `tesla charge start|stop|amps <6-40>` (`ChargeCommandFlow`: never wakes the car, one command, then a read-back; manual console use only, the controller does not call it), `tesla key`, `tesla forget`, `tesla selftest`.
Code: `lib/solar_core/src/tesla/` (`Proto.h`, `TeslaMessages`, `TeslaSession`, `TeslaFlows` (`PairFlow`, `StatusFlow`, `ChargeCommandFlow`), `TeslaSelfTest`, `TeslaBle`), `src/ble/` (`TeslaScan`, `TeslaCar`, `MbedTeslaCrypto`),
`src/platform/NvsTeslaStore`, `src/ble/CarService` (dashboard link: results, readings, 5-minute read-only watch, restore note; `car/CarControl.h` is the portable interface the ApiHandler uses). Host tests: `test/test_tesla` and `test/test_tesla_protocol` (the latter needs OpenSSL; `scripts/native_openssl.py` finds Homebrew's).
Pre-2021 Model S/X are not supported by the protocol; Model 3/Y and 2021+ S/X are expected to work but **paired and read (`tesla pair`, `tesla status`) on a real 2021 Model Y on 2026-10-26, including the encrypted reply**; the charge commands (`tesla charge amps|stop|start`) were reported working on the same car the same day. `tesla debug on` dumps the raw messages. **The full VIN is stored on the board** (NVS `tesla`, needed for signing) but never printed, logged, served or committed; the private key too.
Never echo the typed pair line. Details, code map and the results table: `docs/tesla-car-control.md`; the checklist to run at the car: `docs/tesla-test-plan.md` (update it when results come in).

### Tesla mode (built 2026-10-27, host-tested only; OFF by default, also on a board with a car paired)

**Either-or (2026-11-07):** with Tesla mode on the OCPP server is not started and its loop is skipped (`main.cpp`), so no wallbox is controlled or even connected; with it off the car link is unused.

One switch (Settings > Tesla, `POST /api/car/mode?enabled=&confirm=yes`, console `tesla mode on|off`, restarts the board). Off: `BleGate` false, Bluetooth is never initialised (memory stays free), every car route except mode/forget answers 400 "Tesla mode is off", the Tesla tile and controls are hidden, `ChargeRouter::enforceMode(false)` keeps the charger in command. On: Bluetooth works, the board also opens its own Wi-Fi **`wattsync-tesla`** (AP+STA, per-board password in NVS `tesla`/`appw`, read on request with `GET /api/car/hotspot`, never in telemetry or logs; page at 192.168.4.1), the paired car is put in command by `enforceMode(true)` and "Control through: Charger" is refused. Away from home: start-up waits 25 s for the home Wi-Fi then carries on, retries only when idle (a join attempt interrupts the hotspot), the dashboard posts the phone's clock (`POST /api/clock?epoch=`, only when the board has no time). Setup network beats the hotspot while open. **Tesla mode and charger mode are two separate setups** (decided at boot, `device::Profile`, `solar::profileNamespace/profileFile`): own NVS namespace (`solar` vs `solar-tesla`: mode, current, allowance, hours, prices, goal, messages, charger state) and own history files (`/history*.tsv` vs `/tesla-history*.tsv`: samples, activity, sessions, savings); the first Tesla-mode start begins from defaults. Shared: Wi-Fi/meter/update credentials, the Tesla key and pairing, the hotspot password, diagnostics counters, the phone's display units. Code: `car/TeslaMode.h`, `src/net/TeslaMode.*`, `src/ble/BleGate.h`. Details and the unverified list: `docs/tesla-car-control.md`; tests G1-G12 in `docs/tesla-test-plan.md`.

### Tesla account link, Fleet API (branch `tesla-fleet-api`, 2026-11-07; host-tested, flashed with the link on Bluetooth, never tried on a real account or car)

Settings > Tesla > **Connect through: Bluetooth | Tesla account** (needs Tesla mode on; changing it restarts the board; `POST /api/car/link?mode=ble|api&confirm=yes`).
`car/CarLinkSwitch` forwards the one `CarControl` to `CarService` (Bluetooth) or `fleet/FleetCar` (account); `main.cpp` selects it from `teslaMode.apiLink()` at
start-up and with the account link `BleGate` stays off. `fleet/` is portable (`FleetAuth` single-use refresh tokens saved at once, `FleetApi`, `FleetCar`,
`FleetManager`); reads use REST `vehicle_data` (never wakes), charge commands send the same signed Bluetooth messages through `signed_command` (format ASSUMED).
Board: `net/EspFleetHttp` (TLS pinned to the DigiCert roots in `net/FleetRoots.h`), `net/EspFleetExecutor` (one job task), `platform/NvsFleetStore` (NVS `fleet`:
client id, region, refresh token, app key `fkey`, kept apart from the Bluetooth key), `platform/TeslaIdentity`. Routes `POST /api/car/fleet/connect` (client id,
refresh token, region, VIN, app key in the BODY, never echoed) and `/disconnect`, both registered in `WebServerAdapter`; telemetry `car.mode.link`, `car.account`
(masked client id and a state, no secrets). **Sign-in on the board** (the demo flow): `tools/tesla_fleet/tesla_fleet.py setup-board` puts the app (client id, SECRET, domain, signing key) on the board once (`POST /api/car/fleet/app`, `fleet/FleetApp.h`, NVS `acid/asec/adom/areg`); then Settings > Tesla > Sign in to Tesla (`POST /api/car/fleet/signin` with the page's own origin) opens Tesla's page, Tesla returns to `https://<domain>/callback.html` (`tesla-key-site/callback.html`, hosted on the owner's static site, forwards only to private addresses, `no-referrer`), which sends the browser to the board's `GET /api/car/fleet/callback?code=&state=`; `FleetManager::completeSignIn` checks the one-use 10-minute state, exchanges the code in a task, saves the token and the VIN (one car) or asks for it (`POST /api/car/fleet/vin`). `NvsFleetStore::clear()` (Disconnect) removes only the account, not the app or the key. The computer tool also still has `login` and `register`. Never print a token or
key. Prices and billing: pay-per-use, see `docs/tesla-fleet-api.md`. The Pi answers "needs the ESP32 board".

### Control through: Car (built 2026-10-27, not yet tried on a real car; default Charger)

The planner talks to a `ChargeDevice` (`wallbox/ChargeDevice.h`): `WallboxController` (OCPP) or `car/CarCommander` (Bluetooth), picked by `car/ChargeRouter` (one in command; `main.cpp` passes the router to
`SolarController` and `ApiHandler`; switching calls `SolarController::onCommanderChanged()`). `CarCommander` works from what it BELIEVES the car is doing (the car's last reading, updated by each command it sends; WattSync is the only thing meant to set the amps, so the planner's draw
is what it set, `drawIsMeasured()` is false) and turns the wanted current into one Bluetooth job at a time: charging -> set current; stopped -> set current, then start; stop request -> stop; unplugged/complete/no power -> nothing.
The car is read only to CHECK the belief: first look, after each command (read-back), after an outage, and every 10 min (`kCheckEverySeconds`). Asleep -> nothing, unless `allowWake` (NVS `allowwake`, default OFF, `POST /api/car/allowwake`): then `WakeFlow`
(`TeslaFlows`: status, VCSEC session, ONE signed `RKEAction` WAKE_VEHICLE=30 command `101e`, asks status every 3 s up to 5 looks) at most once per 30 min, only if charging is wanted and the car was last seen plugged in.
Failed jobs back off 30 s / 60 s / 2 min / 5 min; asleep is not an outage. Policy `CarOnReturn` Keep/Tell/Stop after 10+ min out of reach (events "Car not reachable for 10 minutes...", "Car reachable again",
both in the Notifier lists, Java and C++). Events are prefixed "Car:". `CarService` pauses its own watch while controlled. Persisted in NVS `tesla` (`through`, `onreturn`) via `CarSettingsStore`; `restore()` never leaves an unpaired car in command.
Tests: `test/test_car_commander` (39), wake in `test/test_tesla_protocol`, API tests for through/policy, dashboard tests. The decision "ramp to the minimum before stopping" cannot be done when the car is unreachable (nothing can be sent): the car keeps its last setting.

## Documentation map

| For | Read |
|---|---|
| What the app does, modes, settings, endpoints (Pi) | `README.md` |
| ESP32 firmware: set-up, flashing, console, checks on the real wallbox | `esp32-poc/README.md` |
| The ESP32 setup network | `docs/setup-network.md` (with `docs/diagrams/` and `docs/ui/`) |
| Running the Pi app as a service, token, logs | `docs/deploy-to-raspberry-pi.pdf` |
| Scenarios to try on the real house | `docs/test-scenarios.pdf` |
| The owner's user guide (PDF) and the demo GIF | `docs/user-guide.pdf`, `docs/demo/` (MP4 + GIF; rebuild with `docs/demo-src/make_demo.py` then `docs/pdf-src/user_guide_pdf.py`; sample data, see `docs/README.md`) |
| Tesla car control: decisions and the range test | `docs/tesla-car-control.md` |
| Tesla account link (Fleet API): design, set-up, what is untested | `docs/tesla-fleet-api.md` |
| Tesla tests to run at the car (checklist; tick as results come in) | `docs/tesla-test-plan.md` |
| Index of all of the above | `docs/README.md` |

When a feature or rule changes, update the matching file in the same change and say so briefly in the summary.

## Git and rollback

The project is a git repository (created 2026-10-23; before that it was not under version control).

```
main                            current work
before-setup-network            branch: the code as it was before the ESP32 setup network
snapshot-before-setup-network   tag on that same commit (a fixed marker; a branch can move, a tag does not)
before-tesla-car-control        tag on main as it was before the Tesla work was merged (2026-10-29, fast-forward; roll back with git checkout before-tesla-car-control)
before-idtag-forecast-location  tag on main as it was before the ID tag fingerprint, the Tomorrow forecast, the location onboarding and the score wheel were merged (2026-11-01, fast-forward; the branches idtag-hash, forecast and site-location are kept)
before-tesla-fleet-api         tag on main as it was before the Tesla account link (Fleet API), the either-or Tesla mode, the on-board Tesla sign-in and the 24 h / 30 day retention were merged (2026-11-09, fast-forward; the branch tesla-fleet-api is kept)
before-cheapest-hours-and-impact  tag on main as it was before Cheapest hours, the memory diagnostics, goal and plug-in recording, the plan picker and the Impact card were merged (2026-10-31, fast-forward; the branches cheapest-hours, impact-score and memory-diagnostics are kept)
```

The `tesla-car-control` branch (Tesla pairing, status, charge commands, Tesla mode, Control through: Car, the profile isolation, the Settings redesign, dark theme, waiting headline, new day graph) was merged into `main` on 2026-10-29 after a review (secrets scan of the whole history, all three test suites, route registration check). Nothing has been pushed (there is no remote).

- `before-setup-network` is a **reconstruction**. Nothing had been saved before the setup network was added, so the old code was rebuilt by undoing each of
  those edits (every undo checked against the current text). The rebuilt code builds as firmware and passes the 206 host tests that existed then.
- Roll back: `git checkout before-setup-network` to look at the old code (`git checkout main` to return), or `git revert <setup network commit>` to remove the
  feature and keep later work. Compare: `git diff before-setup-network main --stat`.
- The firmware already on a board is not touched by any of this: flash the old code (`git checkout before-setup-network`, then `pio run -e esp32s3-ota -t upload`).
- **Never commit secrets.** `esp32-poc/src/secrets.h` is ignored (root `.gitignore` `**/secrets.h` and `esp32-poc/.gitignore`); only `secrets.example.h` is tracked.
  When the repository was created, the commit history was scanned for the real Wi-Fi name, Wi-Fi password, Enphase token and update password: none appeared.
  `esp32-poc/logs/` (serial monitor logs), the copied `favicon.png`, build output and per-machine state files are ignored too.
- **The global git ignore file on this machine hides every `*.md`**, which silently left `CLAUDE.md` and the READMEs out of the first snapshot. The repository's
  `.gitignore` ends with `!*.md` to override it. After adding a new kind of file, check `git status` really lists it (`git check-ignore -v <file>` says why not).
  `*.log`, `*.patch` and `.idea` are also hidden by that global file; `git add -f` if one of those ever has to be tracked.
- Commit messages end with `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>` when Claude made the change.
- Take a commit before any large change so there is always something to return to.
- **Any major feature gets its own branch** (e.g. `tesla-car-control`), cut from an up-to-date `main`; merge to `main` only when the user says so. Small fixes and docs may go straight to `main`.

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
- With no Wi-Fi the board sits in `bringUpNetwork()`'s loop (`serviceWhileOffline`): anything that must work offline (console, Tesla jobs) has to be serviced there too, and a blocking Wi-Fi join (30 s) must not run while a Tesla job is active. (Found 2026-10-26: `tesla scan` never finished offline.)
- Test output from `mvn` prints WARN lines (fail-safe tests); they are normal.

## Not yet verified on real hardware

Merged 2026-10-31 and flashed, but with no real charge since: Cheapest hours on a real wallbox, a Ready by goal being judged at a real deadline (scenario 70), the plug-in time on a real session (72), Saved by Cheapest hours (73), the plan picker saving on the board (74), the share picture on a phone (75), the Wi-Fi rejoin after a router outage (77). Checklist: `docs/test-scenarios.pdf` section L. The Pi jar with the same recording changes is built but was not deployed (the Pi was not on the network on 2026-10-31).

Verified 2026-10-26: the board's OTA update over Wi-Fi end to end (`esp32s3-ota`, 59 s, board came back serving the new dashboard). Not yet verified: the Tesla dashboard UI on the real board with a real car, Control through: Car on a real charge (planner + belief, restart with the car in command), the wake command (whether the security computer accepts it signed this way), and Bluetooth reads running next to Wi-Fi, OCPP and the Enphase polling (a read pauses the main loop for a few seconds).

The setup network's trial and undo of a bad password, saving a real change, and setup with the router off at start-up (the network opening and an iPhone joining it and loading the page were confirmed on the board 2026-10-26), the board's start-up with no internet, the safe-header collection (`collectHeaders`), the new NVS keys, notifications over https, the departure goal on a real charge, the PSRAM allocator and flash journal at scale, the 3 min / 5 min timing on a real charge, an overnight run watching for crashes. Ideas not built: a native iOS wrapper, a separate total-grid-import cap (Eco's allowance does the same job),
gzip for the page, a hint card for Add to Home Screen, a minimum run time instead of the stop delay.
