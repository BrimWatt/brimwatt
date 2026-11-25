# Documentation

## Contents

| For | Read |
|---|---|
| Overview, modes, settings, endpoints, quick start | [../README.md](../README.md) |
| ESP32 firmware: set-up, flashing, console, checks on a real charger | [../esp32/README.md](../esp32/README.md) |
| The ESP32 setup network (diagrams, UI mocks, endpoints, safety) | [setup-network.md](setup-network.md) |
| Tesla car control over Bluetooth: decisions, research, results on a real car, the dashboard UI | [tesla-car-control.md](tesla-car-control.md) |
| Tesla account link (Fleet API): design, set-up, what is assumed and untested | [tesla-fleet-api.md](tesla-fleet-api.md) |
| Tesla test plan: a checklist to run at the car, with expected results and a result column | [tesla-test-plan.md](tesla-test-plan.md) |
| User guide for the car owner: dashboard, modes, goal, history, settings (PDF) | [user-guide.pdf](user-guide.pdf) |
| Demo video of the dashboard on sample data (MP4, and a GIF of the same) | [demo/brimwatt-demo.mp4](demo/brimwatt-demo.mp4), [GIF](demo/brimwatt-demo.gif) |
| Running the Java app on a Raspberry Pi: service, token, logs, state (PDF) | [deploy-to-raspberry-pi.pdf](deploy-to-raspberry-pi.pdf) |
| Scenarios to try on a real installation (PDF) | [test-scenarios.pdf](test-scenarios.pdf) |
| Working on the project: layout, commands, rules, git notes | [../CLAUDE.md](../CLAUDE.md) |

## Rebuilding the generated files

The pictures, diagrams, videos and PDFs are generated from the scripts in `diagram-src/`, `pdf-src/` and `demo-src/`; edit the script, not the output.
The pictures and demo are taken from the real dashboard page running on made-up sample numbers; nothing in them comes from a real installation.

| Output | Command | Needs |
|---|---|---|
| SVG diagrams in `diagrams/` | `python3 docs/diagram-src/make_diagrams.py` | Python 3 |
| Deploy guide and test scenarios (PDF) | `python3 docs/pdf-src/deploy_pdf.py` and `python3 docs/pdf-src/test_scenarios_pdf.py` | Python 3 with `reportlab` |
| Guide pictures, demo GIF and MP4 | `python3 docs/demo-src/make_demo.py` (or `guide`, `demo` to build one part) | Google Chrome, Pillow; ffmpeg for the MP4 (on the `PATH`, or `FFMPEG=/path/to/ffmpeg`) |
| User guide (PDF) | `python3 docs/pdf-src/user_guide_pdf.py` (after the pictures) | Python 3 with `reportlab` |

Rebuild after the dashboard's look or wording changes. The numbered callouts in the pictures come from `NOTES` in `demo-src/make_demo.py`; the user guide reads the same list.

## Diagrams (`diagrams/`)

| Diagram | Shows |
|---|---|
| [system-overview.svg](diagrams/system-overview.svg) | Envoy, controller (Pi or ESP32), wallbox, phone, and what travels between them |
| [setup-startup-flow.svg](diagrams/setup-startup-flow.svg) | The ESP32's start-up and when the setup network opens |
| [wifi-trial.svg](diagrams/wifi-trial.svg) | A Wi-Fi change is tried and undone if it cannot join |
| [settings-precedence.svg](diagrams/settings-precedence.svg) | Saved values win over `secrets.h`, field by field |

## UI mocks (`ui/`)

Screenshots of the setup page at phone width (402 px), one per state, taken from the real page served by a stand-in for the board's API, so the networks and values are made up. Retake them after changing `esp32/src/web/setup.html`; the states are listed in [setup-network.md](setup-network.md).
