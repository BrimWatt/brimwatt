# Documentation

| What | Where |
|---|---|
| Overview, modes, settings, endpoints (Pi app) | [../README.md](../README.md) |
| ESP32 firmware: set-up, flashing, console, checks | [../esp32-poc/README.md](../esp32-poc/README.md) |
| **The ESP32 setup network** (diagrams, UI mocks, endpoints, safety) | [setup-network.md](setup-network.md) |
| **Tesla car control** (decisions so far, the Phase 0 range test) | [tesla-car-control.md](tesla-car-control.md) |
| Notes for working on the project (layout, commands, rules, git) | [../CLAUDE.md](../CLAUDE.md) |
| Running the Pi app on the Raspberry Pi (service, token, logs, state) | [deploy-to-raspberry-pi.pdf](deploy-to-raspberry-pi.pdf) |
| Scenarios to try on the real house | [test-scenarios.pdf](test-scenarios.pdf) |

## Diagrams (`diagrams/`)

SVG, drawn by `diagram-src/make_diagrams.py`. Edit the script and run it again; do not edit the SVG files by hand.

| Diagram | Shows |
|---|---|
| [system-overview.svg](diagrams/system-overview.svg) | Envoy, controller (Pi or ESP32), wallbox, phone, and what travels between them |
| [setup-startup-flow.svg](diagrams/setup-startup-flow.svg) | The ESP32's start-up and when the setup network opens |
| [wifi-trial.svg](diagrams/wifi-trial.svg) | A Wi-Fi change is tried and undone if it cannot join |
| [settings-precedence.svg](diagrams/settings-precedence.svg) | Saved values win over `secrets.h`, field by field |

```bash
python3 docs/diagram-src/make_diagrams.py
```

## UI mocks (`ui/`)

Screenshots of the setup page at phone width (402 px), one per state. They were taken from the real page served by a stand-in for the board's API, so the networks and values are made up. Retake them after changing `esp32-poc/src/web/setup.html`; the states are listed in [setup-network.md](setup-network.md).

## PDFs

Built from `pdf-src/` (needs Python with `reportlab`). They describe the Raspberry Pi app.

```bash
python3 docs/pdf-src/deploy_pdf.py
python3 docs/pdf-src/test_scenarios_pdf.py
```
