# Tesla car control

> **Status:** design only, plus the Phase 0 range test (built, host-tested, builds as firmware, **not yet run on the board or near a car**). The screens in `ui-drafts/` are proposals and use the old word "wallbox" in places; the decided word is **charger**.

## Decisions so far (2026-10-26)

| Question | Decision |
|---|---|
| Who is it for | Any Tesla owner, with or without a charger. Every controller makes its own key; no key or secret is shipped in the firmware or this repo. |
| Models and years | Aim for all, but **unknown** which accept commands over Bluetooth (older Model S/X may not). Phase 0 shows range only; a compatibility check must come before any promise. |
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
