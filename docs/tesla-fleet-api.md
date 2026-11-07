# Tesla account link (Fleet API)

Branch `tesla-fleet-api`, started 2026-11-07. The second way for Tesla mode to reach the car: **Connect through: Bluetooth | Tesla account**
(Settings > Tesla, shown when Tesla mode is on). Bluetooth stays the default and is unchanged. The account link needs home Wi-Fi with internet and
works from anywhere the board has internet (no Bluetooth range).

**Status: built and host-tested against a pretend Tesla; flashed 2026-11-07 with the link left on Bluetooth. NEVER tried with a real Tesla account or car.**

## How it fits

- Both links are a `CarControl` (`car/CarControl.h`); `car/CarLinkSwitch` forwards to the chosen one. The choice is read once at start-up (changing it
  restarts the board, like Tesla mode); with the account link `BleGate` stays off, so Bluetooth memory stays free.
- Code: `lib/solar_core/src/fleet/` (portable): `FleetAuth` (refresh tokens, single use, saved at once), `FleetApi` (read, wake, signed command),
  `FleetCar` (the `CarControl`, one job at a time in a task), `FleetControl`/`FleetManager` (what the API saves, with validation), `Base64.h`.
  Board: `net/EspFleetHttp` (HTTPS), `net/EspFleetExecutor` (task), `net/FleetRoots.h`, `platform/NvsFleetStore`, `platform/TeslaIdentity`.
- **Reads**: `GET /api/1/vehicles/{vin}/vehicle_data?endpoints=charge_state`. It never wakes the car; a sleeping car answers 408 and is shown as asleep.
- **Charge commands** (start, stop, amps): the SAME signed messages as Bluetooth (`ChargeCommandFlow`), sent one by one through
  `POST .../signed_command {"routable_message": base64}`; the reply is `{"response": base64}`. **Assumed**, not confirmed in Tesla's docs: that the body and
  replies work like this and that one request gets one reply.
- **Wake**: `POST .../wake_up`, then a few reads. Billable and limited to 3 a minute; off unless "allow wake" is on.
- **TLS**: the two DigiCert roots Tesla's hosts chain to (G3 for sign-in, G2 for the API) are pinned in `net/FleetRoots.h`, so the refresh token is not sent
  to an impostor. Needs a correct clock. If Tesla changes authority, the board says "Could not reach Tesla" until the file is updated.
- **Secrets**: the refresh token and the app's signing key live in flash (NVS `fleet`), are never printed, logged or served, and arrive only in a POST body.
  There is NO client secret on the board (refreshing needs only the client id). Telemetry `car.account` shows only a masked client id and a state.
  The app key is kept apart from the Bluetooth key, so connecting an account never replaces a Bluetooth pairing; disconnecting removes it.

## One-time set-up (owner's own Tesla developer app)

1. Developer app with an **Allowed Origin that is a public HTTPS domain** serving the app's public key at
   `/.well-known/appspecific/com.tesla.3p.public-key.pem` (the owner's is hosted on a free static host; localhost does not work for this). Keep
   `http://localhost:3000/oauth/tesla/` as the redirect URI.
2. Billing set up on the developer account with a small limit (the Fleet API is pay-per-use; a billing limit of 0 disables the app).
3. `~/.wattsync/tesla-app.env` (mode 600) with the client id, client secret and domain; the app's private key in `~/.wattsync/tesla-app-private.pem`.
4. `python3 tools/tesla_fleet/tesla_fleet.py register`, then `... login --board http://<board>`: signs in in the browser, lists the cars (masked) and hands the
   sign-in, the VIN and the app key to the board (`POST /api/car/fleet/connect`, secrets in the body). Tesla mode must be on.
5. Settings > Tesla > Connect through: Tesla account (restarts the board).
6. On the phone that is the car's key, open `https://tesla.com/_ak/<domain>` to approve the app's key in the car. Commands fail until then.

## What Tesla's docs say (read 2026-11-07, not all pages were readable)

Pay-per-use billing; $10 a month discount for individual developers; calls below HTTP 500 are billable; billing limit defaults to 0; rate limits per
device per account: realtime data 60/min, wakes 3/min, commands 30/min. Refresh tokens are single use and expire after 3 months. Unsigned commands are
rejected for 2021+ cars. NOT found: per-request prices, access token lifetime, personal-use and distribution terms (Fleet API agreement not read).
**Cost note:** the controller reads the car after every command and every 10 minutes while it is in command; the separate watch starts OFF and runs every
15 minutes when on. Check the real bill after the first week.

## Not verified (needs a real car and account)

Partner registration for the owner's app; the OAuth sign-in; token refresh rotation on the board; `vehicle_data` fields; the `signed_command` format and
replies (including the handshake, and whether Tesla returns broadcasts or empty replies); that the app key approved through Tesla's link can sign; the
wake; request prices. Checklist: `docs/tesla-test-plan.md`, section H.

## Wallbox mode and Tesla mode are either-or

Decided 2026-11-07. With Tesla mode on (either link) the board does NOT run the wallbox side at all: the OCPP server never starts (`main.cpp`,
`startControl`), so no wallbox can connect and nothing can start, stop or limit one. The car is the only thing controlled. With Tesla mode off the board is a
wallbox controller as before and the car link is never used (Bluetooth stays off, API routes answer "Tesla mode is off"). The demo plan this serves: a friend's car
on his own garage charger, with the board at the owner's house reading the owner's solar. A car plugged into a wallbox that WattSync controls is wallbox mode.

## Not built

The Pi (the Java app answers "needs the ESP32 board"); several cars per board; fleet telemetry streaming; the sign-in on the phone itself (today it is a
computer tool, because the redirect goes to localhost); refresh-token expiry warnings (3 months).
