# Documentation

| What | Where |
|---|---|
| Overview, modes, settings, endpoints (Pi app) | [../README.md](../README.md) |
| ESP32 firmware: set-up, flashing, console, checks | [../esp32/README.md](../esp32/README.md) |
| **The ESP32 setup network** (diagrams, UI mocks, endpoints, safety) | [setup-network.md](setup-network.md) |
| **Tesla car control** (decisions, research, what works on a real car, the dashboard UI) | [tesla-car-control.md](tesla-car-control.md) |
| **Tesla test plan** (checklist to run at the car, with expected results and a result column) | [tesla-test-plan.md](tesla-test-plan.md) |
| Notes for working on the project (layout, commands, rules, git) | [../CLAUDE.md](../CLAUDE.md) |
| **User guide** for the car owner (dashboard, modes incl. Cheapest hours, goal, Your impact, history, miles, settings and plans), PDF | [user-guide.pdf](user-guide.pdf) |
| **Demo video** of the dashboard on sample data (MP4, and a GIF of the same) | [demo/wattsync-demo.mp4](demo/wattsync-demo.mp4), [GIF](demo/wattsync-demo.gif) |
| **Pitch to a Tesla owner**: a 46 s video and the message that goes with it (asks them to volunteer as a tester) | [demo/wattsync-tesla-pitch.mp4](demo/wattsync-tesla-pitch.mp4), [demo/neighbor-message.md](demo/neighbor-message.md) |
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

Screenshots of the setup page at phone width (402 px), one per state. They were taken from the real page served by a stand-in for the board's API, so the networks and values are made up. Retake them after changing `esp32/src/web/setup.html`; the states are listed in [setup-network.md](setup-network.md).

## PDFs

Built from `pdf-src/` (needs Python with `reportlab`). They describe the Raspberry Pi app.

```bash
python3 docs/pdf-src/deploy_pdf.py
python3 docs/pdf-src/test_scenarios_pdf.py
```

## User guide and demo (`user-guide.pdf`, `demo/`, `guide/`)

`guide/*.png` are phone-width pictures of the real dashboard page (`src/main/resources/dashboard.html`) running on made-up sample numbers in headless Chrome; `user-guide.pdf` is built from them and `demo/wattsync-demo.mp4` (and a smaller `.gif`) is a captioned slideshow of the same pictures. The pictures carry numbered red callouts and the PDF explains each number; the video steps through them one at a time with a written explanation. The callout list is `NOTES` in `docs/demo-src/make_demo.py` (edit it there, the PDF reads the same list). None comes from a real house, the board or the car. Regenerate after the dashboard's look or wording changes (needs Google Chrome, Pillow and `reportlab`):

```bash
python3 docs/demo-src/make_demo.py        # pictures, GIF and MP4 (or: guide | demo); the MP4 needs ffmpeg: on the PATH, or FFMPEG=/path/to/ffmpeg (e.g. the one from `pip install imageio-ffmpeg`)
python3 docs/demo-src/make_neighbor_demo.py   # the Tesla pitch video (uses docs/demo-src/car_standin.py, a stand-in board with a paired car; same Chrome/ffmpeg needs)
python3 docs/pdf-src/user_guide_pdf.py    # the PDF
```
- `tesla-fleet-api.md`: the Tesla account link (Fleet API) as the second way for Tesla mode to reach the car: design, set-up, what is assumed and untested.
