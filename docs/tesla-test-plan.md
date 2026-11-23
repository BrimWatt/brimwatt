# Tesla: tests to run when the car is available

Written 2026-10-27. Everything below is built and host-tested but **not yet tried on a real car**, except what is marked "done". Tick a box only
when it was run on the car and the expected result happened; write what was seen in the Result column. Update this file as we go (it is in git).

Status key: `[ ]` not run, `[x]` passed, `[!]` failed (say what happened), `[-]` skipped on purpose.

## Before you start

- [ ] The car belongs to someone else: tell its owner what will happen (a key is already in the car under Controls > Locks; WattSync will change its charging current and may wake it).
- [ ] The car is near the board (Bluetooth), **plugged in and awake**. Open a door, or use the Tesla app, to wake it.
- [ ] The board is on Wi-Fi and `http://wattsync.local/` loads. Tesla mode is **off by default**: switch it on in Settings > Tesla first (section G); sections A-F need it on. Settings > Tesla then shows the paired car (masked VIN).
- [ ] Note the car's current setting now (Tesla screen, or Settings > Tesla > Check now): ______ A. Put it back at the end (H).
- [ ] Decide where the wallbox stands: if the car is **not** charged from your wallbox, nothing else is affected. If it is, switching to "Car" stops WattSync commanding the wallbox.
- [ ] Optional but useful: a serial monitor on the cable (`pio device monitor`) for the board's own log lines, and `tesla debug on` to see raw messages.

Back out at any time: Settings > Tesla > **Control through = Charger**. Pause is the kill switch. Forget car removes the key from the board.

## G. Tesla mode and the wattsync-tesla Wi-Fi (new 2026-10-27; do this section first, the others need Tesla mode on)

| # | Test | Expected | Result |
|---|---|---|---|
| G1 | With Tesla mode off (the default), Settings > Tesla | Only the Tesla mode switch, the paired car (masked) and Forget car; no Tesla tile on the dashboard, no badge | [ ] |
| G2 | Console `tesla status` (or any car command) while off | "Tesla mode is off ..." and no Bluetooth starts | [ ] |
| G3 | Memory with Tesla mode off: Diagnostics largest block | At least as high as before (about 53 KB or more); the graph and the three today tiles load | [ ] |
| G4 | Switch Tesla mode on (confirm the dialog) | The board restarts; after about a minute the badge "Tesla mode" shows; Activity "Control moved to the car" once a car is paired; the wallbox is no longer commanded | [ ] |
| G5 | Settings > Tesla > Password > Show | The password appears and hides again after 30 s | [ ] |
| G6 | On a phone, join Wi-Fi `wattsync-tesla` with that password, open `http://192.168.4.1/` | The same dashboard loads (the phone may warn "no internet"; stay on it) | [ ] |
| G7 | Home Wi-Fi still works while the hotspot is open | `http://wattsync.local/` also loads from the home network | [ ] |
| G8 | Away from home (or router off): power-cycle the board with Tesla mode on | Start-up carries on after about 25 s; the hotspot works; the dashboard shows the phone's time within a minute (clock set) | [ ] |
| G9 | Away from home, Custom 8 A, car in command (section D) | Works without the Envoy and without internet | [ ] |
| G10 | Dashboard speed with the hotspot open during a Bluetooth read | The page may pause a few seconds; note how long and whether it times out | [ ] |
| G11 | Open the setup network (hold BOOT 5 s) while Tesla mode is on, then let it close | The hotspot comes back by itself after the setup network closes | [ ] |
| G13 | Settings are separate: in Tesla mode change the mode, allowance and a price, then switch Tesla mode off | Charger mode shows its old values, not the Tesla ones; switching on again shows the Tesla ones; history, activity and sessions are separate too (first Tesla start has empty history) | [ ] |
| G12 | Switch Tesla mode off | The board restarts; the badge goes; `wattsync-tesla` is gone; the charger is in command again | [ ] |

## A. Read only (safe)

| # | Test | Expected | Result |
|---|---|---|---|
| A1 | Settings > Tesla > **Check now** | Within about 10 s the Tesla tile shows battery %, limit, charging state and amps, "Read just now" | [ ] |
| A2 | Leave "Read the car every few minutes" on for 12 minutes with Control = Charger | The tile's "Read ... ago" resets about every 5 minutes; no error in Activity | [ ] |
| A3 | Compare the tile with the car's own screen | Battery, limit and amps agree | [ ] |

## B. Test buttons from the dashboard (the console versions were proven on 2026-10-26)

| # | Test | Expected | Result |
|---|---|---|---|
| B1 | For testing > **Set** current 12 A (confirm the dialog) | Tile and car screen show 12 A within about 15 s; "Put the setting back to ... A" appears | [ ] |
| B2 | **Put back** | Back to the noted value; the "put back" row disappears | [ ] |
| B3 | **Stop**, then **Start** | Car stops, then starts; the result line under the buttons says so | [ ] |

## C. Waking (needs a sleeping car; the wake signing is unverified)

| # | Test | Expected | Result |
|---|---|---|---|
| C1 | Stop charging, leave the car alone 15-30 minutes, then **Check now** | Tile shows "Asleep" (it is not woken by a read) | [ ] |
| C2 | **Wake now** (confirm) | Progress says "Waking the car..."; within about 20 s the result says "The car is awake." and Check now shows awake | [ ] |
| C3 | **Wake now** on an awake car | "The car is already awake." and no command is sent | [ ] |
| C4 | If C2 fails | Paste the exact line shown (and the serial log with `tesla debug on`): a refusal such as "role not allowed" or "did not wake" tells us whether the security computer wants a different signature | [ ] |

## D. Control through: Car, supervised (stay at the car; start gently)

| # | Test | Expected | Result |
|---|---|---|---|
| D0 | Set the mode to **Custom** at a low current (8 A), car charging | Nothing changes yet | [ ] |
| D1 | Settings > Tesla > **Control through = Car**; the dialog names the car (masked VIN) | Cancel keeps Charger. OK: toast "The car is in command"; Activity "Control moved to the car" | [ ] |
| D2 | Wait about 1-2 minutes | Activity "Car: changed limit to 8 A: custom current" (or nothing if it already had 8 A); the car shows 8 A; the tile line says "Charging at 8 A, as set by WattSync" | [ ] |
| D3 | Move the Custom slider to 12 A (it sends after 5 s idle) | Within about 1-2 minutes the car shows 12 A and Activity logs it | [ ] |
| D4 | **Pause** | "Car: stopped charging: paused by you" and the car stops | [ ] |
| D5 | **Resume** | The car restarts (the current is set first, then start); Activity shows "started charging" | [ ] |
| D6 | **Eco** or **Max solar** in daylight | The car follows the sun in whole amps with the planner's timings (start after 3 minutes of surplus, stop after 5 minutes under the minimum, up 2 A / 60 s, down 1 A / 30 s); the Activity reasons make sense | [ ] |
| D7 | End of solar hours (or change Settings > Stop minutes so the window closes soon) | The car is stopped once when the window closes | [ ] |
| D8 | **Control through = Charger** | "Control moved back to the charger"; the wallbox is commanded again (if connected) | [ ] |
| D9 | With the car in command, restart the board (OTA flash or power cycle) | It comes back with the car still in command (and the unreachable policy and wake setting unchanged); if the car was forgotten it must come back on Charger | [ ] |
| D10 | Page and wallbox stay responsive during Bluetooth sessions | The dashboard may lag a few seconds now and then; the OCPP connection to the wallbox stays up; solar readings keep updating | [ ] |

## E. The car goes wrong or out of reach

| # | Test | Expected | Result |
|---|---|---|---|
| E1 | Someone sets a different current on the car's screen | Within 10 minutes (the next check) WattSync sets its own value again | [ ] |
| E2 | Lower the car's own charge limit below its battery % | Within 10 minutes the tile shows it finished; WattSync does not restart it | [ ] |
| E3 | Policy **Keep its setting**: take the board out of Bluetooth range (or the car away) for 12 minutes | Tile line turns amber "Out of reach for ..."; the car keeps its setting; **no** Activity message; back in range it carries on | [ ] |
| E4 | Policy **Just tell me**, same outage | After 10 minutes Activity "Car not reachable for 10 minutes: it keeps its last setting" (and a message if a webhook is set); on return "Car reachable again" | [ ] |
| E5 | Policy **Stop when it is back**, same outage | As E4, then on return the charge is stopped once, then planning resumes (the car restarts by the normal rules) | [ ] |
| E6 | A 3-minute outage with any policy | No messages at all | [ ] |
| E7 | Retry rhythm while out of reach (serial log) | Retries after 30 s, 60 s, 2 min, then every 5 min | [ ] |

## F. Automatic wake (tick "Allow WattSync to wake the car"; car in command, plugged in)

| # | Test | Expected | Result |
|---|---|---|---|
| F1 | With waking **off**, let the car sleep while charging is wanted | Tile line says asleep and waking is off; nothing is sent | [ ] |
| F2 | Tick **Allow WattSync to wake the car**, let it sleep again while charging is wanted | Activity "Car: waking the car so charging can start", then the normal set/start commands | [ ] |
| F3 | Make it sleep again within 30 minutes | No second wake until 30 minutes have passed | [ ] |
| F4 | Unplug the car, let it sleep, ask for charging | It is not woken (nothing to charge) | [ ] |

## H. Clean up

- [ ] The car's current is back to the value noted at the start.
- [ ] Control through = Charger; "Allow WattSync to wake the car" off unless you want it.
- [ ] Tell the car's owner the test is over. Decide: keep the key paired, or **Forget car** here **and** remove the key in the car (Controls > Locks).
- [ ] Record the model, year and firmware of the car in the table in `docs/tesla-car-control.md`.

## What to paste back when something is off

The Activity lines around the time, the Tesla tile text, a screenshot of Settings > Tesla, and (best) the serial log with `tesla debug on`. Never paste the full VIN.

## Known limits to keep in mind while testing

- A sleeping car is only woken if waking is allowed; the read itself never wakes it.
- Every Bluetooth session pauses the board for a few seconds; checks happen about every 10 minutes while in command.
- The planner is told the draw WattSync **set**, not a measurement; the energy count follows it.
- Stepping down to the minimum before an outage is not possible (nothing can be sent to an unreachable car); the car keeps its last setting.


## H. Tesla account link (Fleet API) - built 2026-11-07, never tried

Needs: the owner's developer app set up as in `docs/tesla-fleet-api.md`, a Tesla account that owns or has the car, the car's key approval done through Tesla's link.
Paste the Activity lines / the Tesla tile / the serial log for any failure (never a token or a key).

| # | Test | Expected | Result |
|---|------|----------|--------|
| H1 | `tesla_fleet.py register` | "Register ... ok" | |
| H1b | Tesla mode on, then `tesla_fleet.py setup-board --board ...` | "Put the app on the board: ok" | |
| H2 | Settings > Tesla > Connect through: Tesla account, then **Sign in to Tesla** (phone on the home Wi-Fi), allow the permissions | back on the dashboard: "Signed in. Car found: ..." (or "no car" for an account without one) | |
| H3 | Settings > Tesla > Connect through: Tesla account | board restarts; box says "Connected to your Tesla account" | |
| H4 | Open `tesla.com/_ak/<domain>` on the key phone, approve in the car | key appears in the car (Controls > Locks) | |
| H5 | Check now (car awake) | charge state, limit, current shown | |
| H6 | Check now (car asleep) | "asleep", nothing woken | |
| H7 | For testing > Charging Stop, then Start | the car's state changes; each prints a read-back | |
| H8 | For testing > Current 10 A, then Put back | the car's request follows | |
| H9 | Wake now (allow wake) | awake within about 30 s, billed once | |
| H10 | Switch Control through: Car; plan a few minutes of solar | limit follows the planner like Bluetooth | |
| H11 | Router off for 2 minutes during a charge | car keeps its setting; message after 10 min | |
| H12 | Leave it a day, then check the token still works | no "Sign in again" | |
| H13 | Disconnect, then Connect through: Bluetooth | Bluetooth pairing still works (its key was never replaced) | |
| H14 | Look at the Tesla developer billing page after a week | requests and cost as expected | |
