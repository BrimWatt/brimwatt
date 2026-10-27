# Tesla car control

> **Status:** built (2026-10-27, **not yet tried on a real car**): `tesla pair <VIN>`, read-only `tesla status`, `tesla charge start|stop|amps <n>`, `tesla key|forget|selftest` (section "First connecting step: what was built"). Before that: design and research (below), plus the Phase 0 range test (built, host-tested, builds as firmware, run once on the board (2026-10-26): one Tesla heard, weak signal (-80 dBm, 4 sightings in 60 s) from where the board sat; closer placement and the car-asleep case not yet tried). The screens in `ui-drafts/` are proposals and use the word "charger".

## Decisions so far (2026-10-26)

| Question | Decision |
|---|---|
| Who is it for | Any Tesla owner, with or without a charger. Every controller makes its own key; no key or secret is shipped in the firmware or this repo. |
| Models and years | Aim for all that can. Research (2026-10-26, below): **pre-2021 Model S/X are out**; Model 3, Model Y and 2021+ S/X should work but are not tested by us. Nothing is promised until a real car has been paired. |
| Who commands the charge | **One at a time**: the car or the charger. Setting: *Control through: Charger \| Car*. With a car on Bluetooth its readings (battery, rate, limit) are still used. |
| Word for the hardware | "Charger" in anything the user sees. Code and saved settings keep their names until a separate rename. |
| Where it runs | ESP32 and Pi. (Pi: BlueZ is needed; a small helper using Tesla's own open-source command library is one option. Not verified.) |
| Car can't be reached | Step down to the minimum current before stopping, keep the last setting, retry every 30 s with backoff, a message after 10 min. Owner picks: keep last setting (default), just tell me, or stop when it is back. The car's own charge limit and scheduled charging stay as backstops. |
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

Console commands on the ESP32 (see `esp32-poc/README.md`): `tesla pair <VIN>`, `tesla status`, `tesla charge start|stop|amps <6-40>`, `tesla key`, `tesla forget`, `tesla selftest`. `tesla pair` changes the car's key list (it needs the owner's key card); `tesla charge ...` (added later the same day, see below) is the only thing that changes how the car charges. `tesla status` changes nothing.

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

**What is NOT verified (needs a real car):** the car's actual replies; that the add-key request is accepted with the Charging Manager role (the fallback to Driver is not built yet); the Bluetooth connection, indications, fragmenting and MTU on the S3; the **reply decryption** (the protocol description has no example for it, so the code follows the written rules: signature type 9, domain, VIN, counter, flags, request hash = 0x05 + our tag, fault; if the car disagrees, `tesla status` will say "the car's reply could not be verified" and shows nothing); `tesla selftest` on the board's mbedTLS; Bluetooth next to Wi-Fi; range.

### First real-car finding (2026-10-26, 2021 Model Y): the security-computer payload is a bare `UnsignedMessage`

The first `tesla pair` reached the car over Bluetooth (found, connected, request sent, car answered in under a second) but the car refused with fault 10 ("could not decode"), twice. Cause: add-key, status and key-lookup messages were wrapped in `ToVCSECMessage { signedMessage { ... } }`, the older non-routable format. Tesla's protocol notes say `RoutableMessage.protobuf_message_as_bytes` for the security domain parses as a bare `vcsec.UnsignedMessage`. Fixed in `TeslaMessages.cpp` (`vcsecPayload`); add-key is now `82010e 2a08 ... 2006 3202 0809`, status is `0a00`. Re-tried the same evening: **pairing worked** (the car sent several "waiting" broadcasts, then a final whitelist status with no error, which the board treats as accepted). The pair log also prints the raw request and reply bytes (public data only) while this is being proven; remove them once pairing works. A one-time retry as a phone key (form factor 7) after fault 10 stays in as a fallback.

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
| 2021 Model Y, firmware not recorded | **OK 2026-10-26** (Charging Manager role, cloud-key form factor, key card tap; about 7 s from request to accept; board ~-77 dBm, car in garage) | not tried yet | not tried yet | Needed the bare-`UnsignedMessage` fix first (see "First real-car finding"). Confirm the key shows in the car under Controls > Locks. |
