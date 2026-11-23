# Tesla car control

> **Status:** built and **proven on a real 2021 Model Y on 2026-10-26** (console only; the controller does not use it yet): `tesla pair <VIN>`, read-only `tesla status`, `tesla charge start|stop|amps <n>`, `tesla key|forget|selftest` (section "First connecting step: what was built"). Before that: design and research (below), plus the Phase 0 range test (built, host-tested, builds as firmware, run once on the board (2026-10-26): one Tesla heard, weak signal (-80 dBm, 4 sightings in 60 s) from where the board sat; closer placement and the car-asleep case not yet tried).

## Decisions so far (2026-10-26)

| Question | Decision |
|---|---|
| Who is it for | Any Tesla owner, with or without a charger. Every controller makes its own key; no key or secret is shipped in the firmware or this repo. |
| Models and years | Aim for all that can. Research (2026-10-26, below): **pre-2021 Model S/X are out**; Model 3, Model Y and 2021+ S/X should work but are not tested by us. Nothing is promised until a real car has been paired. |
| Who commands the charge | **One at a time**: the car or the charger. Setting: *Control through: Charger \| Car*. With a car on Bluetooth its readings (battery, rate, limit) are still used. |
| Word for the hardware | "Charger" in anything the user sees. Code and saved settings keep their names until a separate rename. |
| Where it runs | ESP32 and Pi. (Pi: BlueZ is needed; a small helper using Tesla's own open-source command library is one option. Not verified.) |
| Car can't be reached | Keep the last setting (nothing can be sent to an unreachable car, so stepping down to the minimum first is not possible), retry after 30 s, 60 s, 2 min, then every 5 min, a message after 10 min. Owner picks: keep (default), just tell me, or stop when it is back. The car's own charge limit and scheduled charging stay as backstops. |
| Not now | Cloud (Fleet API) fallback, several cars per controller. |

## Phase 0: the range test

**Without a VIN:** `tesla scan` (or `tesla scan 120`) lists every Tesla the board hears: its name hash, a verdict, the share of 5-second windows it was heard in, and the signal, strongest first. Yours is most likely the first; scan again with the car moved to check. It also lists neighbours' cars (names are not stored). A device that advertises the Tesla service without a name shows as "(no name)". Whether a sleeping car still advertises is unverified.

**With a VIN** (more precise, a clear verdict for your car alone). Type in the serial monitor (the VIN is used for the scan only, never saved, and only its first 3 and last 4 characters are printed):

```
tesla scan <VIN> 60
```

The board computes the name the car advertises (`S` + the first 8 bytes of SHA-1 of the VIN in lowercase hex + `C`, from Tesla's published protocol description; the example in that description is a host test), listens for that many seconds without connecting, then prints how many times it heard the car, in how many 5-second windows, and the signal (dBm), with a verdict: Not heard, Weak, OK or Good. `tesla stop` ends it early. Run it with the board where it will live, with the car in its usual spot, and again with the car asleep.

What it cannot tell you: whether the car accepts charging commands over Bluetooth. The Wi-Fi side may answer a little slower while the radio listens.

## Compatibility and pairing: research (2026-10-26)

Sources are Tesla's open-source `vehicle-command` repository (README, `protocol.md`, `vcsec.proto`, `keys.proto`, `car_server.proto`, `tesla-control` README) and the `yoziru/esphome-tesla-ble` project (an ESP32 doing this today). Nothing below was run by us yet; "stated" means a source says it, "inferred" means we are reading between lines.

### Which cars

| Question | Finding |
|---|---|
| Pre-2021 Model S and X | **Not supported** by the new protocol (stated in Tesla's README). Those cars stay internet-API only. |
| Model 3, Model Y, 2021+ Model S/X | Expected to work (inferred: not excluded; community tools use them). No official model list was found. Treat as "should work, check on the car". |
| Tesla's official list | Not found. The Fleet API docs are the place to look. |

**What the owner can check without any code:** a car that lets them add a phone key and a key card (Controls > Locks) has the hardware. The real test is the pairing below on that car.

### How a controller talks to the car

- Bluetooth LE service `00000211-b2d1-43f0-9b88-960cebf8b91e`; write characteristic `...0212...` (write with response), read `...0213...`. Each message is prefixed with a 2-byte big-endian length. The car advertises as `S` + first 8 bytes of SHA-1(VIN) in hex + `C` (what Phase 0 already scans for).
- Two "domains" with their own keys, sessions and clocks: **VCSEC** (locks, wake, key management; works even when the screen computer sleeps; up to 3 simultaneous BLE connections shared with phones and fobs) and **Infotainment** (everything else, including charging; needs the car awake).
- Session: client sends its public key, the car answers with its own, an epoch, a counter and its clock; both derive `K = SHA1(ECDH-P256 secret x)[:16]`; later commands are authenticated, over BLE with AES-GCM (random 12-byte nonce). Counters only go up within an epoch. Responses are encrypted only on firmware 2024.38 and later.
- Charging actions exist as plain protobuf messages: **set charging amps** (`SetChargingAmpsAction`, int32 amps), **start / stop charging** (`ChargingStartStopAction`), **set charge limit** (`ChargingSetLimitAction`, percent). Valid ranges are not in the files we read.
- **Roles** (`keys.proto`): OWNER 2, DRIVER 3, FM 4 (fleet manager; cannot use BLE), VEHICLE_MONITOR 5, **CHARGING_MANAGER 6**, GUEST 8. The protocol notes say Charging Manager can authorize commands that affect charging. The ESP32 project lists charging manager as enough for start/stop, amps, limit, opening the charge port and basic info.

### Pairing: what the owner does

Same flow in Tesla's tools and in the ESP32 project:

1. The controller makes its own P-256 key pair (private key never leaves the board).
2. Owner sits in the car, controller in Bluetooth range.
3. Controller sends an **add-key request** carrying its public key, a role and a form factor (VCSEC `WhitelistOperation`; form factors include phone and "cloud key").
4. **The owner taps their existing NFC key card on the centre console**, then confirms on the car's screen if it asks.
5. The car lists a new key (Controls > Locks). The owner can rename it ("WattSync") and delete it any time; that revokes the controller.

The Tesla app approval link (`tesla.com/_ak/<domain>`) is for the cloud (Fleet API) route and needs a domain we do not have; **not used here**.

### Design for the first connecting step

Goal: prove the whole chain on one real car with the smallest, safest step, and read only.

1. **Key**: generate the P-256 key on first use, store the private key in NVS (ESP32) or a file with owner-only permissions (Pi). Never printed, never in the repo; show only a short fingerprint of the public key.
2. **Role**: ask for **Charging Manager** (least power that can do the job: no locks, no driving). Fall back to Driver only if the car rejects it, and tell the owner. Roles cannot be changed later without pairing again.
3. **Console command** `tesla pair <VIN>` (ESP32): checks the car is heard (the Phase 0 scan), connects, sends the add-key request, prints "Tap your key card on the centre console now", waits up to about 60 s, reports accepted / refused / timed out. Needs the full VIN once; it is kept only as the 8-byte hash the car advertises, never stored whole or printed.
4. **Then `tesla status`**: open a session and read the charge state (battery %, charge limit, charger amps, plugged in). Still no commands that change anything. This is the compatibility test: success on a given model and year is recorded in this document.
5. **Only after that**: set amps, start/stop, wake, the "Control through: Car" mode and the Pi path (Phases 2+).

### Risks and open questions

- **Crypto and protobuf on the ESP32-S3.** P-256 ECDH, SHA-1/256, HMAC and AES-GCM are available in mbedTLS (part of the ESP32 Arduino core), protobuf can be done with nanopb or hand-written. An existing ESP32 project does it, but its docs warn against its generic board package on S3/C3 variants (a packaging note, not a stated hardware limit). Our S3 is **unverified**.
- **Bluetooth and Wi-Fi share one radio.** Phase 0 already saw the web page slow down while scanning; a connected session may be worse. To measure.
- **Range**: the first scan heard the car at -80 dBm from where the board sits; connecting needs a steadier link. Likely the deciding practical issue for a garage controller.
- **Firmware changes**: Tesla changes the protocol over time (responses encrypted from 2024.38). Pin what we tested.
- **Valid amps range and the charge-state field names** are in `vehicle.proto`, which we have not read yet. Read it before step 4.
- Source caveat: the two read-only summaries of the repository files were produced by a fetch tool, so exact numbers (UUIDs, enum values) should be checked against the repository when the code is written. The host tests will pin them.

## First connecting step: what was built (2026-10-27)

Console commands on the ESP32 (see `esp32/README.md`): `tesla pair <VIN>`, `tesla status`, `tesla charge start|stop|amps <6-40>`, `tesla key`, `tesla forget`, `tesla selftest`. `tesla pair` changes the car's key list (it needs the owner's key card); `tesla charge ...` (added later the same day, see below) is the only thing that changes how the car charges. `tesla status` changes nothing.

**Code map**

| Piece | Where |
|---|---|
| Protobuf writer/reader (just what we need) | `lib/solar_core/src/tesla/Proto.h` |
| Messages to build and read (add key, status, charge state, the envelope) | `TeslaMessages.*` |
| Secure session: handshake check, signed prefix, encrypted command, reply check | `TeslaSession.*` |
| Pairing, status and charge commands as step-by-step logic with no radio | `TeslaFlows.*` (`PairFlow`, `StatusFlow`, `ChargeCommandFlow`) |
| Crypto interface; mbedTLS on the board, OpenSSL in host tests | `TeslaCrypto.h`, `src/ble/MbedTeslaCrypto.*`, `test/support/OpenSslCrypto.h` |
| Tesla's published example values, runnable on the board | `TeslaSelfTest.*` (`tesla selftest`) |
| Bluetooth link, one job at a time, radio released after | `src/ble/TeslaCar.*` |
| Key and VIN in flash (namespace `tesla`) | `src/platform/NvsTeslaStore.*` |

**What is verified (on the computer):** the message layouts byte for byte, Tesla's example key agreement, session-info check, signed-data prefix and AES-GCM command (all match the published values), pairing and status against a pretend car (asleep car is not woken, unknown key, wrong VIN, damaged reply, silent car). 29 tests in `test/test_tesla_protocol` (19 for pairing and status, 10 for the charge commands).

**Update 2026-10-26 (2021 Model Y):** pairing, the encrypted session with the main computer, the signed read command and the decryption of its reply all worked on a real car (`tesla status` printed awake/unlocked, charging, battery 45 %, limit 80 %, drawing 18 A at 239 V). Bugs found on the way and fixed: bare `UnsignedMessage` payload, Bluetooth `deinit(true)`, Tesla jobs not serviced offline, a wrapped inactivity timer, broadcasts mistaken for replies, a BLE client freed too early. Charge commands (start, stop, amps) were then also reported working by the owner on the same car. Still not verified: waking, a Driver-role fallback (not needed so far), a sleeping car, Bluetooth next to a working Wi-Fi.

**Originally not verified (before that test):** the car's actual replies; that the add-key request is accepted with the Charging Manager role (the fallback to Driver is not built yet); the Bluetooth connection, indications, fragmenting and MTU on the S3; the **reply decryption** (the protocol description has no example for it, so the code follows the written rules: signature type 9, domain, VIN, counter, flags, request hash = 0x05 + our tag, fault; if the car disagrees, `tesla status` will say "the car's reply could not be verified" and shows nothing); `tesla selftest` on the board's mbedTLS; Bluetooth next to Wi-Fi; range.

### First real-car finding (2026-10-26, 2021 Model Y): the security-computer payload is a bare `UnsignedMessage`

The first `tesla pair` reached the car over Bluetooth (found, connected, request sent, car answered in under a second) but the car refused with fault 10 ("could not decode"), twice. Cause: add-key, status and key-lookup messages were wrapped in `ToVCSECMessage { signedMessage { ... } }`, the older non-routable format. Tesla's protocol notes say `RoutableMessage.protobuf_message_as_bytes` for the security domain parses as a bare `vcsec.UnsignedMessage`. Fixed in `TeslaMessages.cpp` (`vcsecPayload`); add-key is now `82010e 2a08 ... 2006 3202 0809`, status is `0a00`. Re-tried the same evening: **pairing worked** (the car sent several "waiting" broadcasts, then a final whitelist status with no error, which the board treats as accepted). The raw message dump is now behind `tesla debug on` (off by default). A one-time retry as a phone key (form factor 7) after fault 10 stays in as a fallback.

### Charge commands: start, stop, set amps (built 2026-10-27, not tried on a real car)

`tesla charge start`, `tesla charge stop`, `tesla charge amps <6-40>` (typing the command is the confirmation; nothing else asks). Each runs one `ChargeCommandFlow`:
security computer status first, then a session with the main computer, then **exactly one** command, then a read-only charge-state read-back so the result is printed from the car's own answer.

| Command | What is sent (CarServer `VehicleAction`) | Wire bytes |
|---|---|---|
| start | `chargingStartStopAction` (field 6) with `start` (2) | `120432021200` |
| stop | `chargingStartStopAction` (6) with `stop` (5) | `120432022a00` |
| amps N | `setChargingAmpsAction` (43) with `charging_amps` (1) | N=16: `1205da02020810` |

Field numbers read from Tesla's `car_server.proto` (vehicle-command repo) on 2026-10-27; the byte layouts are pinned in host tests.

Safety rules in the code (all host-tested):
- Amps outside 6-40 send nothing (checked in the console, in `TeslaCar` and again in the flow).
- A sleeping car is **not woken** and gets no command; the text says to wake it from the Tesla app. (Waking is a separate command, not built.)
- An unknown key, a wrong VIN or a failed session send nothing.
- A refusal (for example not plugged in) is shown with the car's own reason, and no read-back follows.
- Silence after the command went out says "may or may not have been applied, check with `tesla status`". `tesla stop` during a command says the same.
- A failed read-back never turns an accepted command into a failure.
- One job at a time; a scan or another Tesla job must be stopped first.

Not verified: all of it on a real car (the car's replies, whether the Charging Manager role may start/stop and set amps over Bluetooth, and the encrypted reply, as for status). Not built: wake, charge limit, the "Control through: Charger | Car" switch, the Pi, any automatic use by the controller. These commands are manual console tests only; the controller does not call them.

**Decision changed: the VIN is stored on the board.** Earlier notes said "never store a VIN". Signing needs it (the VIN is part of what is signed), so after a successful pairing the full VIN is kept in the board's flash, beside the key. It is never printed (only `5YJ**********1234`), logged, shown in the API or dashboard, or put in the repository. The flash is not encrypted: someone holding the board could read it. `tesla forget` deletes it.

**Privacy of the console:** the typed `tesla pair <VIN>` line is never echoed back by the board. The serial monitor on your own computer does show what you type (`monitor_echo`), as with every command.

**Trying it on a real car (suggested order)**
1. `tesla selftest` on the board (no car): must say all values check out.
2. `tesla scan` with the car where it will park; check the signal.
3. In the car, with your key card in hand: `tesla pair <VIN>`; tap the card on the centre console when told, confirm on the screen. Then Controls > Locks should list a new key.
4. `tesla status` with the car awake (open a door, or the car just driven); then again asleep: expect "asleep" and no charge data.
5. Only then, with the car plugged in and awake and the real wallbox NOT in charge (one commander at a time): `tesla charge amps 8`, check the car's screen and `tesla status`; `tesla charge stop`; `tesla charge start`. Compare with what the car and the Tesla app show.
6. Write the model, year and result in the table below.

| Model / year / firmware | Pair | Status | Charge cmds | Notes |
|---|---|---|---|---|
| 2021 Model Y, firmware not recorded | **OK 2026-10-26** (Charging Manager role, cloud-key form factor, key card tap; about 7 s from request to accept; board ~-77 dBm, car in garage) | **OK 2026-10-26**: awake, charging, battery, limit, current and voltage read from the car (encrypted reply verified) | **OK 2026-10-26**: `amps` (10, 16, 18 A), `stop` and `start` all worked; each prints the car's own read-back | Needed the bare-`UnsignedMessage` fix first (see "First real-car finding"). Confirm the key shows in the car under Controls > Locks. |

### More real-car findings (2026-10-26)

- The car's reply counter is **not** a running total (it was 1 for different replies in one session), so the "counter must rise" check was dropped; a reply only verifies against the exact request it answers.
- The car also broadcasts messages addressed to "everyone" (domain 0) during a conversation; only messages addressed to our routing id count.
- `BLEDevice::deinit(true)` makes the radio unusable until a reboot (and aborts on a second release): use `deinit(false)`. Free the `BLEClient` only after the disconnect event arrived.
- Typing into the serial monitor while the board is inside a 30 s Wi-Fi join delays the reply; arrow keys inject escape codes into the line.

### Dashboard UI (built 2026-10-26, checked in the browser on a stand-in, not yet on the board)

- **Decisions (owner, 2026-10-26):** the paired car is read-only by default; "Control through: Car" will need a confirmation naming the car; test controls get UI too; one car slot (pair another and the first is replaced). Context: the paired car belongs to a friend, used for testing while the board keeps optimizing the owner's own wallbox.
- **Settings > Tesla:** VIN box + Pair (the VIN is cleaned and checked in the page, sent in a POST body, then the box is emptied), a progress box while pairing or reading (Stop button), then paired state: masked VIN, "Read the car every few minutes" (watch), Check now, Forget car, and **For testing** (Start, Stop, current + Set, "Put the setting back to N A" after a change). Every test button asks first.
- **Tesla tile** (under the modes, only when paired): battery with the limit marked on a thin bar, charging state, amps and kW, "Read 2m ago" (amber after 15 min), "Not reachable" with the reason when the last read failed, "Asleep" when the car sleeps, and the charger's own amps for comparison.
- **Pi:** the group says "Car control needs the ESP32 board."
- **Not built:** the "Control through: Charger | Car" switch and the controller engine behind it; wake; Pi support.
- **Watch schedule:** first read 60 s after start, then every 5 minutes (a Bluetooth read pauses the main loop for a few seconds, so not every minute; if OCPP or the web page stalls, the next step is to run Bluetooth in its own task).
- **Code:** `lib/solar_core/src/car/CarControl.h` (interface), `ApiHandler` (routes, telemetry `car`), `src/ble/CarService.*`, `TeslaCar` (results, readings), dashboard `renderCar`.

### Control through: Car, the engine (built 2026-10-27, host-tested, not yet tried on a car)

- **Where:** `wallbox/ChargeDevice.h` (what the planner needs from whatever holds the charge), `car/CarCommander.*`, `car/ChargeRouter.*`, `car/CarSettings.h`; API `POST /api/car/through` and `/api/car/policy`; dashboard Settings > Tesla > Control through. 30 tests in `test/test_car_commander`.
- **Switching:** the page asks first (naming the masked VIN); the API needs `confirm=yes` and a paired car. Switching releases any fixed hold on the wallbox, resets the planner, logs "Control moved to the car" / "back to the charger". The choice is saved; a forgotten car can never stay in command.
- **What it sends:** from the car's last reading (must be under 150 s old, otherwise it reads first): charging and the current differs -> set the current; stopped -> set the current, then start; stop wanted and charging -> stop; unplugged, complete, no power -> nothing; asleep -> nothing. One job at a time, the latest wish wins. Pause, Custom's fixed current, the departure goal, the daylight window and the fail-safe all work through the same calls as with the wallbox.
- **Unreachable car:** retries after 30 s, 60 s, 2 min, then every 5 min; the car keeps its last setting. After 10 minutes out of reach: *Keep* is silent, *Just tell me* reports it and the return, *Stop when it is back* also stops the charge once on return, then planning resumes. A sleeping car is a different state ("asleep", retried every 2 min), not an outage.
- **Known limits:** (1) no wake command, so a car that fell asleep after a stop is not restarted until it wakes (the Tesla app wakes it); (2) every Bluetooth read or command pauses the board's main loop for a few seconds (about one read a minute while in command): if the web page, OCPP or the Enphase polling stall because of it, the next step is to run Bluetooth in its own task; (3) the car's current is whole amps and its measured current is a rounded number; (4) not tried on a real car: the first real test should be a short one with the owner at the car, Control through = Car in Eco or Custom, watching the Activity list and the car's screen.

### Beliefs, checks and waking (built 2026-10-27, host-tested, not yet tried on a car)

- **WattSync does not measure the car.** It is the only thing meant to set the car's current, so the draw the planner uses is what WattSync last set. The dashboard already words that honestly ("set to N A", "up to X kW" when a draw is not measured).
- **The car is read to check, not to measure:** when WattSync takes command, after each command (the read-back), after an outage, and every 10 minutes. The check catches a car that stopped by itself (charge limit, unplugged), a change made on the car or in the Tesla app (WattSync sets its value again), or a car that fell asleep. Routine one-minute reads are gone, so the board's main loop pauses for a Bluetooth session about 10 times less often. The energy count (history, savings, departure goal) follows the assumed draw, so a car that quietly stops is over-counted for up to 10 minutes.
- **Waking (off by default):** Settings > Tesla > "Allow WattSync to wake the car when charging is wanted". Only when charging is wanted, the car is asleep and was last seen plugged in (not unplugged, not full, not never-seen), at most once every 30 minutes. A wake keeps the car awake about 10-15 minutes and uses a little battery. A manual **Wake now** button (For testing) and `tesla wake` exist to prove it first.
- **Protocol:** `RKEAction` (UnsignedMessage field 2) = `RKE_ACTION_WAKE_VEHICLE` (30), bytes `101e`, sent to the security computer as an AES-GCM command in a session (the same signing as the charge commands). `WakeFlow` looks first, sends the command once, then asks `GET_STATUS` every 3 s, up to 5 looks (about 20 s), and reports "did not wake" if the car still sleeps. **Unverified on a real car:** Tesla's notes say the official library signs every domain with AES-GCM over Bluetooth, but the security computer may want HMAC for this command; if the car refuses, the console/dashboard shows its fault text and the fix is in `WakeFlow`.
- **Trying it:** leave the car alone until it sleeps (about 15-30 minutes after charging stops), then `tesla wake` (or Wake now) and watch `tesla status` change from asleep to awake. Record the result in the table above.

### Tesla mode: Bluetooth off until switched on, the wattsync-tesla Wi-Fi, the car in command (built 2026-10-27, host-tested, not yet on a car)

Decided with the owner: **off by default, also on a board that already has a car paired**; the owner turns it on when testing. Settings > Tesla > **Tesla mode** (restarts the board, about a minute).
- **Separate data (2026-10-28):** Tesla mode and charger mode are two setups chosen at boot (`device::Profile`): NVS namespace `solar` vs `solar-tesla` (settings, charger state) and history files `/history*.tsv` vs `/tesla-history*.tsv`. Shared: credentials, the Tesla key/VIN, the hotspot password. Existing boards keep their charger data untouched (the `solar` names did not change).
- **Off:** `device::BleGate` stays false, so `BLEDevice::init` is never called (its memory stays free, the board never pauses for Bluetooth). Every Bluetooth entry point (`TeslaCar` start*, `TeslaScan`, `CarService`) refuses with "Tesla mode is off"; the API answers 400 for every car route except `/api/car/mode` and `/api/car/forget` (deleting our key needs no radio). The dashboard hides the Tesla tile and every car control; a paired car is shown masked with Forget. `ChargeRouter::enforceMode(false)` puts the charger back in command (once a second, also after a restart that restored the car).
- **On:** Bluetooth works; the board opens its own Wi-Fi **`wattsync-tesla`** (WPA2, a 10-character password made on the board at first use and kept in NVS `tesla`/`appw`, shown only on request via `GET /api/car/hotspot`, never in telemetry or logs; page at `http://192.168.4.1/`) next to the home Wi-Fi (AP+STA); `enforceMode(true)` puts a paired car in command (also when pairing finishes later), and "Control through" is fixed to the car (`through=charger` is refused). The wallbox is not commanded and keeps its last current. Header badge "Tesla mode" (shortened to "Tesla" under 380 px).
- **Away from home** (no home Wi-Fi, no Envoy, no internet): start-up waits 25 s for the home Wi-Fi, then carries on; Wi-Fi retries every 10 min only when nobody is using the hotspot and no Bluetooth job runs (a join attempt blocks the board and hops the radio's channel, which would interrupt the hotspot). No Envoy means only Custom can plan; no internet means no network time, so the dashboard posts the phone's clock (`POST /api/clock?epoch=`, only accepted while the board has no time) and the planner and history start. The setup network has priority over the hotspot while it is open; the hotspot returns when it closes.
- Code: `lib/solar_core/src/car/TeslaMode.h` (interface, `generateHotspotPassword`), `ChargeRouter::enforceMode`, `ApiHandler` (`/api/car/mode`, `/api/car/hotspot`, `/api/clock`, telemetry `car.mode`), `src/net/TeslaMode.*`, `src/ble/BleGate.h`, `NvsTeslaStore` (`mode`, `appw`; `clear()` now only removes key, vin, through), console `tesla mode on|off`.
- Not verified on the board: Wi-Fi hotspot + Bluetooth + home Wi-Fi together (memory and stalls), the away-from-home start-up, the phone joining `wattsync-tesla`, iOS behaviour on a Wi-Fi with no internet, whether the Bluetooth pause makes the hotspot page time out.
