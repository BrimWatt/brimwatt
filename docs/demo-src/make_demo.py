#!/usr/bin/env python3
"""Builds the user guide screenshots (docs/guide/*.png) and the demo animation (docs/demo/wattsync-demo.gif).

No Pi or board is needed. Each picture is the REAL dashboard page (src/main/resources/dashboard.html) running against a
stand-in for the controller's API that returns made-up sample numbers, loaded in headless Chrome at phone width.
Run from the repository root:   python3 docs/demo-src/make_demo.py
Needs: Google Chrome (macOS app) and Pillow.
"""
import json
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
PAGE = (ROOT / "src/main/resources/dashboard.html").read_text()
CHROME = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
OUT_GUIDE = ROOT / "docs/guide"
OUT_DEMO = ROOT / "docs/demo"
WIDTH = 402

# --- sample numbers (made up) ---------------------------------------------------------------------------------------------

SHIM = r"""<script>
(function () {
  const C = __CONFIG__;
  const dayStart = Math.floor(Date.now() / 1000 / 86400) * 86400, H = h => dayStart + h * 3600;
  const nowT = H(11.5);
  const tele = {
    currentMode: C.mode, paused: C.paused, gridAllowanceWatts: 600, chargerConnected: true, enphaseOk: true, rateCents: 30,
    peakRateCents: 0, peakStartHour: 16, peakEndHour: 21, fastModeAmps: C.fastAmps || 24, maxAmps: 32, notifyTo: '',
    daylight: { enabled: true, open: C.daylightOpen, startMinutes: 30, endMinutes: 60, window: '07:00 to 17:00' },
    power: { voltage: 240, solarWatts: C.solar, gridWatts: C.grid, chargerWatts: C.chargerW, homeWatts: C.home },
    charger: { status: C.status, amps: C.amps, limitAmps: C.amps, drawMeasured: true },
    goal: { kwh: C.goalKwh, byMinutes: 420, doneKwh: C.goalDone, forcing: false, forcedAmps: 0 },
    controller: { hasReading: true, reason: C.reason || 'no change', gridAllowanceWatts: 600, failSafe: false, readingAgeSeconds: 8, pollSeconds: 60 }
  };
  const samples = [];
  for (let m = 6 * 60 + 30; m <= 11 * 60 + 30; m += 5) {
    const x = (m - 6.5 * 60) / (11.5 * 60 - 6.5 * 60), sun = Math.round(8200 * Math.sin(Math.min(1, x * 0.95 + 0.05) * Math.PI / 2) ** 1.6);
    const home = 350 + ((m * 37) % 5) * 90, car = m >= 8 * 60 ? Math.min(7200, Math.max(1440, sun - home)) : 0;
    samples.push([H(m / 60), sun, home, Math.round(home + car - sun), car, 0, car > 0 ? 1 : 0]);
  }
  const hist = { today: Math.floor(dayStart / 86400), dayStart, now: nowT, daysAgo: 0, daylight: { start: H(7), end: H(17) }, samples,
    totals: { carKwh: C.carToday, carSolarKwh: C.carSolarToday, savedDollars: C.carSolarToday * 0.3, solarKwh: 31.2, importKwh: 1.1, exportKwh: 6.4 } };
  const days = []; const car = [22, 14, 31, 0, 18, 26, 9, 35, 20, 12, 28, 16, 24, 0, 30];
  for (let k = 0; k < 30; k++) { const c = car[k % car.length]; days.push([hist.today - k, 0, c, Math.round(c * 0.8 * 10) / 10, 0, 0, Math.round(c * 0.8 * 0.3 * 100) / 100]); }
  const sessions = { total: 3, open: C.charging ? { start: H(8), end: nowT, kwh: C.carToday, sunKwh: C.carSolarToday, sunPct: 82, avgAmps: 28.5, saved: C.carSolarToday * 0.3, mode: 'ECO' } : null,
    sessions: [{ start: H(-14), end: H(-9.5), kwh: 24.8, sunKwh: 9.1, sunPct: 37, avgAmps: 16, saved: 2.73, mode: 'FAST', reason: 'EVDisconnected' },
               { start: H(-32), end: H(-27), kwh: 21.7, sunKwh: 18.4, sunPct: 85, avgAmps: 28, saved: 5.52, mode: 'ECO', reason: 'Remote' }] };
  const events = { total: 4, events: [
    { t: H(11.2), text: 'Charging at 30 A (' + '7.2 kW), 82% from the sun' }, { t: H(10.1), text: 'Limit raised to 30 A' },
    { t: H(8.0), text: 'Charging started at 7 A' }, { t: H(7.9), text: 'Car plugged in' } ] };
  const diag = { now: nowT, uptimeSeconds: 183000, clockSet: true, lastRestart: { reason: 'Power on', crash: false }, restarts: 3, crashes: 0, lastCrash: null,
    memory: { freeKb: 214, minFreeKb: 160 }, wifiRssi: -58, firmwareBuilt: 'sample' };
  const map = { '/api/telemetry': tele, '/api/days': { today: hist.today, days }, '/api/history': hist, '/api/sessions': sessions, '/api/events': events, '/api/diagnostics': diag };
  window.fetch = url => { const p = String(url).split('?')[0]; return Promise.resolve({ ok: p in map, status: p in map ? 200 : 404, json: async () => map[p], text: async () => '' }); };
  try { localStorage.setItem('unit', C.miles ? 'miles' : 'kwh'); localStorage.setItem('mpk', '3'); } catch (e) {}
})();
</script>
"""

AFTER = r"""<script>
setTimeout(() => {
  const C = __CONFIG__;
  if (C.range && C.range !== 'day') setRange(C.range);
  if (C.settings) { openSettings(); document.querySelectorAll('details.opt').forEach(d => { const n = d.querySelector('summary').firstChild.textContent.trim(); d.open = (C.open || []).includes(n); }); }
  if (C.diag) { document.getElementById('diag').open = true; }
  if (C.scrollTo) { let t = document.querySelector(C.scrollTo); t = t && (t.closest('section') || t); if (t) window.scrollTo(0, t.getBoundingClientRect().top + scrollY - 8); }
  if (C.scrollY) window.scrollTo(0, C.scrollY);
}, 1800);
</script>
"""

MARKS = r"""<script>
setTimeout(() => {
  const C = __CONFIG__;
  if (!C.marks) return;
  const W = innerWidth, Hh = innerHeight;
  const box = (css, text) => { const d = document.createElement('div'); d.style.cssText = 'position:fixed;pointer-events:none;box-sizing:border-box;' + css; if (text !== undefined) d.textContent = text; document.body.appendChild(d); return d; };
  const taken = [];
  const find = spec => { const [sel, up] = spec.split('@'); let t = document.querySelector(sel); if (t && up) t = t.closest(up) || t; return t && t.getBoundingClientRect(); };
  C.marks.forEach(([spec]) => { const r = find(spec); if (r && r.width) taken.push([Math.max(2, r.left - 11), Math.max(2, r.top - 11), Math.max(2, r.left - 11) + 22, Math.max(2, r.top - 11) + 22]); });
  C.marks.forEach(([spec, n, label, pref]) => {
    const [sel, up] = spec.split('@');
    let t = document.querySelector(sel); if (!t) return; if (up) t = t.closest(up) || t;
    const r = t.getBoundingClientRect(); if (!r.width || r.top > Hh - 10 || r.bottom < 10) return;
    box('z-index:99998;left:' + (r.left - 3) + 'px;top:' + (r.top - 3) + 'px;width:' + (r.width + 6) + 'px;height:' + (r.height + 6) + 'px;border:2px solid #e11d48;border-radius:10px');
    box('z-index:100000;left:' + Math.max(2, r.left - 11) + 'px;top:' + Math.max(2, r.top - 11) + 'px;width:22px;height:22px;border-radius:50%;background:#e11d48;color:#fff;font:700 13px/22px -apple-system,Helvetica,sans-serif;text-align:center;box-shadow:0 1px 4px rgba(0,0,0,.4)', String(n));
    if (label) {
      const p = box('z-index:100000;left:0;top:0;max-width:' + (W - 12) + 'px;padding:3px 8px;border-radius:9px;background:#e11d48;color:#fff;font:600 12px/16px -apple-system,Helvetica,sans-serif;white-space:nowrap;box-shadow:0 1px 4px rgba(0,0,0,.35)', label);
      const w = p.offsetWidth, h = p.offsetHeight, clampX = x => Math.min(Math.max(6, x), W - w - 6);
      const tries = [[clampX(r.right - w - 14), r.bottom + 5], [clampX(r.right - w - 14), r.top - h - 5], [clampX(r.left + 14), r.bottom + 5], [clampX(r.left + 14), r.top - h - 5],
                     [clampX(r.right - w - 14), r.bottom - h - 6], [clampX(r.left + 14), r.top + 6]];
      if (pref === 'up') tries.unshift(tries[1]);
      const free = ([x, y]) => y > 2 && y + h < Hh && !taken.some(q => x < q[2] && x + w > q[0] && y < q[3] && y + h > q[1]);
      const [x, y] = tries.find(free) || tries[0];
      p.style.left = x + 'px'; p.style.top = y + 'px';
      taken.push([x, y, x + w, y + h]);
    }
  });
}, 2500);
</script>
"""

BASE = dict(mode="ECO", paused=False, fastAmps=24, daylightOpen=True, solar=7800, grid=-150, chargerW=7200, home=400,
            status="CHARGING", amps=30, goalKwh=0, goalDone=0, charging=True, carToday=9.6, carSolarToday=7.9, miles=False)

SCENES = {
    # name: overrides
    "charging":      {},
    "charging-goal": dict(goalKwh=20, goalDone=9.6),
    "miles":         dict(miles=True, goalKwh=20, goalDone=9.6),
    "max-solar":     dict(mode="MAX_SOLAR"),
    "custom":        dict(mode="FAST", solar=2200, grid=5100, chargerW=5760, home=450, amps=24, fastAmps=24, carSolarToday=3.1),
    "paused":        dict(paused=True, status="SUSPENDED_EVSE", amps=0, chargerW=0, grid=-6900, home=900, charging=False, reason="paused"),
    "unplugged":     dict(status="AVAILABLE", amps=0, chargerW=0, grid=-7000, home=800, charging=False, carToday=0, carSolarToday=0),
    "night":         dict(daylightOpen=False, status="PREPARING", amps=0, chargerW=0, solar=0, grid=450, home=450, charging=False, carToday=0, carSolarToday=0),
    "history-7":     dict(range="7"),
    "history-30":    dict(range="30"),
    "settings":      dict(settings=True, open=[]),
    "settings-goal": dict(settings=True, open=["Charge by morning", "Units"], goalKwh=20, goalDone=9.6),
    "settings-miles": dict(settings=True, open=["Charge by morning", "Units"], miles=True, goalKwh=20, goalDone=9.6),
    "settings-diag": dict(settings=True, open=[], diag=True),
}


def build(scene, extra=None):
    cfg = dict(BASE, **SCENES[scene], **(extra or {}))
    html = PAGE.replace("<script>\nconst el", SHIM.replace("__CONFIG__", json.dumps(cfg)) + "<script>\nconst el", 1)
    html = html.replace("</body>", AFTER.replace("__CONFIG__", json.dumps(cfg)) + MARKS.replace("__CONFIG__", json.dumps(cfg)) + "</body>")
    return html


def shoot(scene, path, height, scale, extra=None):
    """Headless Chrome will not make a window narrower than about 500 px, so the page goes in a 402 px wide frame and the
    picture is cut to the frame."""
    with tempfile.TemporaryDirectory() as tmp:
        (Path(tmp) / "page.html").write_text(build(scene, extra))
        outer = Path(tmp) / "outer.html"
        outer.write_text(f'<!doctype html><body style="margin:0;background:#fff"><iframe src="page.html" width="{WIDTH}" height="{height}" style="border:0;display:block"></iframe></body>')
        raw = Path(tmp) / "raw.png"
        cmd = [CHROME, "--headless=new", "--disable-gpu", "--hide-scrollbars", f"--window-size=600,{height}",
               f"--force-device-scale-factor={scale}", "--virtual-time-budget=6000", f"--screenshot={raw}", outer.as_uri()]
        subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=90)
        Image.open(raw).crop((0, 0, WIDTH * scale, height * scale)).save(path)


def font(size, bold=False):
    for p in ("/System/Library/Fonts/Helvetica.ttc", "/Library/Fonts/Arial.ttf", "/System/Library/Fonts/SFNS.ttf"):
        if Path(p).exists():
            try:
                return ImageFont.truetype(p, size, index=1 if bold and p.endswith(".ttc") else 0)
            except Exception:
                pass
    return ImageFont.load_default()


def trim(img):
    """Cuts empty page background off the bottom of a tall capture."""
    px = img.convert("RGB")
    bg = px.getpixel((2, px.height - 2))
    y = px.height - 1
    while y > 0 and all(px.getpixel((x, y)) == bg for x in range(0, px.width, 7)):
        y -= 1
    return img.crop((0, 0, img.width, min(img.height, y + 24)))


# Callouts. name -> [(element, short label shown in the video, longer explanation printed in the PDF)].
# "#id@.cls" marks the nearest ancestor with that class. The number is the position in the list.
NOTES = {
    "top": [
        ("#hero", "What is happening now", "The headline says what the charger is doing right now, and why when it is not charging."),
        ("#flowgrid", "Where the power goes", "Solar is what the panels make, Home what the house uses, Car what the charger draws, Grid what is bought or sent out."),
        ("#stats", "Today's totals", "Money saved by using the sun, the share of the car's energy that came from it, and the energy the car took."),
        ("#conn", "Live", "Turns amber ('Meter 12m old') when the solar readings are getting stale."),
        ("#gear", "Settings", "Opens Settings: solar hours, goal, prices, limits, units and messages."),
    ],
    "top-goal": [
        ("#hero", "Goal progress", "With a goal set, the headline shows progress, for example 'Goal: 12 of 20 kWh by 7:00 am'."),
        ("#pause-btn", "Pause", "A kill switch that stops the charge and blocks any start until you tap Resume."),
    ],
    "top-miles": [
        ("#charger-power", "Miles per hour", "In miles mode the Car box shows miles of range added per hour instead of kW."),
        ("#st-car-label@.stat", "Range today", "The tile counts estimated miles of range added today."),
    ],
    "top-paused": [
        ("#hero", "Paused by you", "Nothing will start while paused, whatever the mode or the sun."),
        ("#pause-btn", "Resume", "Tap Resume to hand control back to the mode you had."),
    ],
    "top-unplugged": [("#hero", "No car plugged in", "Charging starts by itself once a car is plugged in.")],
    "top-night": [
        ("#hero", "Outside solar hours", "Eco and Max solar run only in the daylight window. Custom still works."),
        ("#solar-power@.ft", "Resting until sunrise", "At night the solar box says it is resting."),
    ],
    "modes-eco": [
        ("#seg", "Pick a mode", "Eco, Max solar or Custom. Tap one."),
        ("#allowance-panel", "Grid allowance", "How much grid power the car may add to the sun. Zero means sun only."),
    ],
    "modes-max": [
        ("#seg", "Max solar", "Uses everything the panels make and ignores the house."),
        ("#allowance-panel", "Top-up only", "The allowance only tops the car up to the next whole amp."),
    ],
    "modes-custom": [
        ("#seg", "Custom", "A fixed charging current, sun or no sun."),
        ("#fast-panel", "Set the current", "Slide to pick 6 to 40 amps. It can use grid power day or night."),
    ],
    "graph-day": [
        ("#tabs", "Day, week, month, year", "Switch the time range."),
        ("#chart", "Touch to read it", "Solar (yellow), car (blue), home (grey) and grid (red). Touch the graph for exact numbers."),
    ],
    "graph-7": [
        ("#tabs", "7 days", "One bar per day. Yellow came from the sun, red from the grid."),
        ("#range-summary", "The total", "Energy the car took, and what the sun saved you."),
    ],
    "graph-30": [
        ("#tabs", "30 days", "A month of days at a glance."),
        ("#range-summary", "The total", "Energy the car took, and what the sun saved you."),
    ],
    "activity": [
        ("#feedcard", "Activity", "A short list of what the controller did and when."),
        ("#sessions", "Charging sessions", "Each charge: when, how much, how much from the sun, and what you saved."),
    ],
    "settings": [
        ("#daylight-panel", "Solar hours", "Keeps Eco and Max solar to daylight. Set the start and stop margins."),
        ("#diag", "Diagnostics", "A short health list: uptime, restarts, memory and Wi-Fi signal."),
    ],
    "settings-goal": [
        ("#goal-kwh@.field", "How much", "The energy you want by then. Turn the switch off to stop."),
        ("#goal-time@.field", "By when", "The time you need the car ready."),
        ("#unit-select@.field", "Units", "Show energy as kWh or as estimated miles."),
    ],
    "settings-miles": [
        ("#goal-kwh@.field", "Goal in miles", "Type the goal in miles."),
        ("#goal-hint-row", "What is sent", "The whole kWh actually sent to the charger."),
        ("#unit-select@.field", "Show as miles", "Switches the whole dashboard to estimated miles."),
        ("#mpk-input@.field", "Your miles per kWh", "Your car's real figure from the wall (1 to 6, starts at 3)."),
    ],
    "settings-diag": dict(settings=True, open=[], diag=True),
}


def build(scene, extra=None):
    cfg = dict(BASE, **SCENES[scene], **(extra or {}))
    html = PAGE.replace("<script>\nconst el", SHIM.replace("__CONFIG__", json.dumps(cfg)) + "<script>\nconst el", 1)
    html = html.replace("</body>", AFTER.replace("__CONFIG__", json.dumps(cfg)) + MARKS.replace("__CONFIG__", json.dumps(cfg)) + "</body>")
    return html


def shoot(scene, path, height, scale, extra=None):
    """Headless Chrome will not make a window narrower than about 500 px, so the page goes in a 402 px wide frame and the
    picture is cut to the frame."""
    with tempfile.TemporaryDirectory() as tmp:
        (Path(tmp) / "page.html").write_text(build(scene, extra))
        outer = Path(tmp) / "outer.html"
        outer.write_text(f'<!doctype html><body style="margin:0;background:#fff"><iframe src="page.html" width="{WIDTH}" height="{height}" style="border:0;display:block"></iframe></body>')
        raw = Path(tmp) / "raw.png"
        cmd = [CHROME, "--headless=new", "--disable-gpu", "--hide-scrollbars", f"--window-size=600,{height}",
               f"--force-device-scale-factor={scale}", "--virtual-time-budget=6000", f"--screenshot={raw}", outer.as_uri()]
        subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=90)
        Image.open(raw).crop((0, 0, WIDTH * scale, height * scale)).save(path)


def font(size, bold=False):
    for p in ("/System/Library/Fonts/Helvetica.ttc", "/Library/Fonts/Arial.ttf", "/System/Library/Fonts/SFNS.ttf"):
        if Path(p).exists():
            try:
                return ImageFont.truetype(p, size, index=1 if bold and p.endswith(".ttc") else 0)
            except Exception:
                pass
    return ImageFont.load_default()


def trim(img):
    """Cuts empty page background off the bottom of a tall capture."""
    px = img.convert("RGB")
    bg = px.getpixel((2, px.height - 2))
    y = px.height - 1
    while y > 0 and all(px.getpixel((x, y)) == bg for x in range(0, px.width, 7)):
        y -= 1
    return img.crop((0, 0, img.width, min(img.height, y + 24)))


# Callouts. name -> [(element, short label shown in the video, longer explanation printed in the PDF)].
# "#id@.cls" marks the nearest ancestor with that class. The number is the position in the list.
NOTES = {
    "top": [
        ("#hero", "What is happening now", "The headline says what the charger is doing right now, and why when it is not charging."),
        ("#flowgrid", "Where the power goes", "Solar is what the panels make, Home what the house uses, Car what the charger draws, Grid what is bought or sent out."),
        ("#stats", "Today's totals", "Money saved by using the sun, the share of the car's energy that came from it, and the energy the car took."),
        ("#conn", "Live", "Turns amber ('Meter 12m old') when the solar readings are getting stale."),
        ("#gear", "Settings", "Opens Settings: solar hours, goal, prices, limits, units and messages."),
    ],
    "top-goal": [
        ("#hero", "Goal progress", "With a goal set, the headline shows progress, for example 'Goal: 12 of 20 kWh by 7:00 am'."),
        ("#pause-btn", "Pause", "A kill switch that stops the charge and blocks any start until you tap Resume."),
    ],
    "top-miles": [
        ("#charger-power", "Miles per hour", "In miles mode the Car box shows miles of range added per hour instead of kW."),
        ("#st-car-label@.stat", "Range today", "The tile counts estimated miles of range added today."),
    ],
    "top-paused": [
        ("#hero", "Paused by you", "Nothing will start while paused, whatever the mode or the sun."),
        ("#pause-btn", "Resume", "Tap Resume to hand control back to the mode you had."),
    ],
    "top-unplugged": [("#hero", "No car plugged in", "Charging starts by itself once a car is plugged in.")],
    "top-night": [
        ("#hero", "Outside solar hours", "Eco and Max solar run only in the daylight window. Custom still works."),
        ("#solar-power@.ft", "Resting until sunrise", "At night the solar box says it is resting."),
    ],
    "modes-eco": [
        ("#seg", "Pick a mode", "Eco, Max solar or Custom. Tap one."),
        ("#mode-help", "What it does", "A sentence under the buttons describes the chosen mode."),
        ("#allowance-panel", "Grid allowance", "How much grid power the car may add to the sun. Zero means sun only."),
    ],
    "modes-max": [
        ("#seg", "Max solar", "Uses everything the panels make and ignores the house."),
        ("#allowance-panel", "Top-up only", "The allowance only tops the car up to the next whole amp."),
    ],
    "modes-custom": [
        ("#seg", "Custom", "A fixed charging current, sun or no sun."),
        ("#fast-panel", "Set the current", "Slide to pick 6 to 40 amps. It can use grid power day or night."),
    ],
    "graph-day": [
        ("#tabs", "Day, week, month, year", "Switch the time range."),
        ("#chart", "Touch to read it", "Solar (yellow), car (blue), home (grey) and grid (red). Touch the graph for exact numbers."),
    ],
    "graph-7": [
        ("#tabs", "7 days", "One bar per day. Yellow came from the sun, red from the grid."),
        ("#range-summary", "The total", "Energy the car took, and what the sun saved you."),
    ],
    "graph-30": [
        ("#tabs", "30 days", "A month of days at a glance."),
        ("#range-summary", "The total", "Energy the car took, and what the sun saved you."),
    ],
    "activity": [
        ("#feedcard", "Activity", "A short list of what the controller did and when."),
        ("#sessions", "Charging sessions", "Each charge: when, how much, how much from the sun, and what you saved."),
    ],
    "settings": [
        ("#daylight-panel", "Solar hours", "Keeps Eco and Max solar to daylight. Set the start and stop margins."),
        ("#diag", "Diagnostics", "A short health list: uptime, restarts, memory and Wi-Fi signal."),
    ],
    "settings-goal": [
        ("#goal-kwh@.field", "How much", "The energy you want by then. Turn the switch off to stop."),
        ("#goal-time@.field", "By when", "The time you need the car ready."),
        ("#unit-select@.field", "Units", "Show energy as kWh or as estimated miles."),
    ],
    "settings-miles": [
        ("#unit-select@.field", "Show as miles", "Switches the whole dashboard to estimated miles."),
        ("#mpk-input@.field", "Your miles per kWh", "Your car's real figure from the wall (1 to 6, starts at 3)."),
        ("#goal-kwh@.field", "Goal in miles", "Type the goal in miles."),
        ("#goal-hint-row", "What is sent", "The whole kWh actually sent to the charger."),
    ],
    "settings-diag": [("#diag", "Diagnostics", "Setup check, uptime, last restart, free memory and Wi-Fi signal.")],
}


def marks_for(name, only=None):
    """Callout list for the page script. With `only` (0-based) just that one is drawn, keeping its number."""
    return [[sel, i + 1, ""] for i, (sel, _short, _long) in enumerate(NOTES.get(name, [])) if only is None or only == i]


# name, scene, extra, window height (css px). Window shots show what the phone shows; the page is never stitched.
GUIDE = [
    ("top",            "charging",      {}, 500),
    ("top-goal",       "charging-goal", {}, 500),
    ("top-miles",      "miles",         {}, 500),
    ("top-paused",     "paused",        {}, 420),
    ("top-unplugged",  "unplugged",     {}, 420),
    ("top-night",      "night",         {}, 420),
    ("modes-eco",      "charging",      dict(scrollTo="#modes"), 430),
    ("modes-max",      "max-solar",     dict(scrollTo="#modes"), 430),
    ("modes-custom",   "custom",        dict(scrollTo="#modes"), 560),
    ("graph-day",      "charging",      dict(scrollTo="#chart"), 520),
    ("graph-7",        "history-7",     dict(scrollTo="#chart"), 520),
    ("graph-30",       "history-30",    dict(scrollTo="#chart"), 520),
    ("activity",       "charging",      dict(scrollTo="#feed"), 780),
    ("settings",       "settings",      {}, 1180),
    ("settings-goal",  "settings-goal", {}, 1180),
    ("settings-miles", "settings-miles", {}, 1180),
    ("settings-diag",  "settings-diag", {}, 1180),
]


def guide_shots():
    OUT_GUIDE.mkdir(parents=True, exist_ok=True)
    for name, scene, extra, height in GUIDE:
        shoot(scene, OUT_GUIDE / f"{name}.png", height, 2, dict(extra, marks=marks_for(name)))
        print("guide", name)


def caption_frame(path, text, sub, k=1):
    """Adds the caption bar under a picture taken at k times the phone's pixel size. The second part wraps to two lines."""
    img = Image.open(path).convert("RGB")
    f_sub = font(15 * k)
    probe = ImageDraw.Draw(Image.new("RGB", (4, 4)))
    lines, cur = [], ""
    for word in sub.split():
        trial = (cur + " " + word).strip()
        if probe.textlength(trial, font=f_sub) > img.width - 28 * k and cur:
            lines.append(cur)
            cur = word
        else:
            cur = trial
    lines.append(cur)
    bar = (44 + 21 * len(lines) + 12) * k
    canvas = Image.new("RGB", (img.width, img.height + bar), (15, 23, 42))
    canvas.paste(img, (0, 0))
    d = ImageDraw.Draw(canvas)
    d.text((14 * k, img.height + 9 * k), text, font=font(17 * k, True), fill=(255, 255, 255))
    for i, line in enumerate(lines):
        d.text((14 * k, img.height + (40 + 21 * i) * k), line, font=f_sub, fill=(226, 232, 240))
    return canvas


def find_ffmpeg():
    """ffmpeg from $FFMPEG, the PATH, or the one bundled with the imageio-ffmpeg package if that is installed."""
    import os
    import shutil
    found = os.environ.get("FFMPEG") or shutil.which("ffmpeg")
    if found:
        return found
    try:
        import imageio_ffmpeg
        return imageio_ffmpeg.get_ffmpeg_exe()
    except Exception:
        return None


FRAMES = [
    # scene, extra, seconds, callouts (a NOTES name), title, explanation
    ("charging", {}, 5.0, "top", "Charges from your spare solar",
     "The headline says what is happening. The flow shows the sun, the house, the grid and the car. Sample data."),
    ("charging", dict(scrollTo="#modes"), 5.0, "modes-eco", "Pick how it charges",
     "Eco uses only spare solar, Max solar uses everything the panels make. The grid allowance sets how much grid power may top it up."),
    ("custom", dict(scrollTo="#modes"), 4.5, "modes-custom", "Need a fast charge? Use Custom",
     "A fixed current from 6 to 40 amps, sun or no sun. It can use grid power."),
    ("charging-goal", dict(scrollY=0), 4.5, "top-goal", "Set a goal for the morning",
     "Say how much and by when. WattSync charges only as fast as needed to get there. Pause stops everything."),
    ("history-7", dict(scrollTo="#chart"), 4.5, "graph-7", "See what the sun saved you",
     "One bar per day: yellow from the sun, red from the grid. Day, week, month and year views."),
    ("miles", {}, 4.5, "top-miles", "Prefer miles? Switch the units",
     "Energy shows as estimated range added. Miles are the energy times your car's miles per kWh."),
    ("settings-miles", {}, 5.0, "settings-miles", "Settings: goal and units",
     "Choose miles, enter your own miles per kWh, and type the goal in miles. The charger still gets whole kWh."),
    ("paused", {}, 4.0, "top-paused", "Pause is a kill switch",
     "Stops the charge and blocks any start, whatever the mode, until you tap Resume."),
    ("night", {}, 4.0, "top-night", "At night it rests",
     "Eco and Max solar sleep until sunrise. Custom still works."),
]


def demo():
    OUT_DEMO.mkdir(parents=True, exist_ok=True)
    big, durations = [], []
    with tempfile.TemporaryDirectory() as tmp:
        n = 0
        for scene, extra, secs, notes, title, sub in FRAMES:
            # first an overview of the screen with every callout numbered, then one callout at a time with its explanation
            steps = [(None, title, sub, 3.0)]
            for i, (_sel, short, long) in enumerate(NOTES[notes]):
                steps.append((i, f"{i + 1}.  {short}", long, 4.0 + 0.25 * max(0, len(long) // 40 - 2)))
            for only, head, text, secs in steps:
                p = Path(tmp) / f"f{n}.png"
                shoot(scene, p, 820, 2, dict(extra, marks=marks_for(notes, only)))
                big.append(caption_frame(p, head, text, 2))
                durations.append(secs)
                n += 1
            print("demo scene", scene, len(steps), "frames")
        # GIF: half size, small palette
        small = [f.resize((f.width // 2, f.height // 2), Image.LANCZOS) for f in big]
        pal = [f.quantize(colors=128, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE) for f in small]
        out = OUT_DEMO / "wattsync-demo.gif"
        pal[0].save(out, save_all=True, append_images=pal[1:], duration=[int(d * 1000) for d in durations], loop=0, optimize=True, disposal=2)
        print("demo", out, round(out.stat().st_size / 1e6, 2), "MB")
        # MP4 (H.264, plays everywhere): needs ffmpeg
        ff = find_ffmpeg()
        if not ff:
            print("no ffmpeg found: skipped the MP4 (pip install imageio-ffmpeg, or install ffmpeg)")
            return
        lines = []
        for i, (f, d) in enumerate(zip(big, durations)):
            path = Path(tmp) / f"v{i}.png"
            f.save(path)
            lines.append(f"file '{path}'\nduration {d}")
        lines.append(f"file '{Path(tmp) / f'v{len(big) - 1}.png'}'")   # the concat list needs the last file repeated
        lst = Path(tmp) / "list.txt"
        lst.write_text("\n".join(lines))
        mp4 = OUT_DEMO / "wattsync-demo.mp4"
        subprocess.run([ff, "-y", "-loglevel", "error", "-f", "concat", "-safe", "0", "-i", str(lst), "-vf", "fps=30,format=yuv420p",
                        "-c:v", "libx264", "-crf", "20", "-movflags", "+faststart", str(mp4)], check=True)
        print("demo", mp4, round(mp4.stat().st_size / 1e6, 2), "MB")


if __name__ == "__main__":
    if not Path(CHROME).exists():
        sys.exit("Google Chrome not found at " + CHROME)
    what = sys.argv[1] if len(sys.argv) > 1 else "all"
    if what in ("all", "guide"):
        guide_shots()
    if what in ("all", "demo"):
        demo()
