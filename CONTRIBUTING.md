# Contributing to WattSync

Thanks for your interest. WattSync controls real charging equipment, so changes are reviewed for safety and correctness first.

## Getting set up

Follow the [Quick start](README.md#quick-start) to build either implementation, and [`esp32/README.md`](esp32/README.md) for the firmware.
Nothing in the tests needs a board, a charger or a car.

```bash
mvn -o test                                                              # Java app
node --test tests/dashboard/dashboard.test.mjs                           # the dashboard page (Node 18 or newer)
cd esp32 && .venv/bin/pio test -e native                                 # C++ core on your computer
```

All three suites must pass before a change is merged. On macOS the C++ tests need
`CPLUS_INCLUDE_PATH=$(xcrun --show-sdk-path)/usr/include/c++/v1`.

## Ground rules

- **The two controllers must behave the same.** The planner and control rules exist twice: Java (`src/main/java/com/wattsync/charging`)
  and C++ (`esp32/lib/solar_core/src/planner`). A rule change goes into both, with the same numbers and tests in both.
- **Test what you change.** The timing rules (start, stop, smoothing, daylight window, fail-safe) are encoded in tests; update them with the rule.
- **One dashboard page.** `src/main/resources/dashboard.html` is served by the Pi and embedded in the firmware. After editing it,
  rebuild both the jar and the firmware. Logic you want tested goes in a plain `function` so the dashboard tests can load it.
- **New API routes** are registered in the portable handler (`esp32/lib/solar_core/src/web/ApiHandler.*`), in the Pi's routes
  (`web/ApiRoutes.java`) and, for the board, in `esp32/src/web/WebServerAdapter.cpp`. Every changing request needs the `X-WattSync: 1` header.
- **No secrets, no personal data.** Never commit tokens, keys, passwords, VINs, real coordinates, Wi-Fi names or screenshots of a real
  installation. `esp32/src/secrets.h` is git-ignored; use made-up values in tests and sample data in pictures.
- **Plain words in the UI.** Short, no jargon; say "solar", not "sun".
- **Keep the documentation current.** A change to behaviour updates the README or the matching file under `docs/` in the same commit.

## Submitting a change

1. Open an issue first for anything larger than a fix, so the approach can be agreed.
2. Keep each pull request to one topic, with a clear title in the imperative ("Add ...", "Fix ...") and a description of what changed and why.
3. Say what you tested, and what you could not test (for example "not tried on a real charger"). Untested hardware behaviour should be called out, not implied.
4. For changes that touch how current is set or how a charge is started or stopped, describe the failure cases you considered.

## Licensing

WattSync is licensed under the [GNU Affero General Public License v3.0](LICENSE). By contributing you agree that your contribution is
licensed under the same terms.

## Reporting security problems

Please do not open a public issue for a vulnerability: see [SECURITY.md](SECURITY.md).
