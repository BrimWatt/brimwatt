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
    html = html.replace("</body>", AFTER.replace("__CONFIG__", json.dumps(cfg)) + "</body>")
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
        shoot(scene, OUT_GUIDE / f"{name}.png", height, 2, extra)
        print("guide", name)


def caption_frame(path, text, sub):
    img = Image.open(path).convert("RGB")
    bar = 64
    canvas = Image.new("RGB", (img.width, img.height + bar), (15, 23, 42))
    canvas.paste(img, (0, 0))
    d = ImageDraw.Draw(canvas)
    d.text((14, img.height + 9), text, font=font(17, True), fill=(255, 255, 255))
    d.text((14, img.height + 36), sub, font=font(12), fill=(148, 163, 184))
    return canvas


FRAMES = [
    # scene, extra, seconds, title, subtitle
    ("charging", {}, 3.5, "Charges from your spare solar", "Live flow: sun, house, grid and car. Sample data."),
    ("charging", dict(scrollTo="#modes"), 3.0, "Pick how it charges", "Eco, Max solar or Custom; pause any time."),
    ("charging-goal", dict(scrollY=0), 3.0, "Set a goal for the morning", "Say how much, and by when. Sample data."),
    ("history-7", dict(scrollTo="#chart"), 3.0, "See what the sun saved you", "Day, week, month and year views."),
    ("miles", {}, 3.5, "Prefer miles? Switch the units", "Energy shown as estimated range added."),
    ("settings-miles", {}, 3.5, "Settings: goal and units", "Set your own miles per kWh."),
    ("paused", {}, 3.0, "Pause is a kill switch", "Stops the charge and blocks any start."),
    ("night", {}, 3.0, "At night it rests", "Solar modes sleep until sunrise."),
]


def demo():
    OUT_DEMO.mkdir(parents=True, exist_ok=True)
    frames, durations = [], []
    with tempfile.TemporaryDirectory() as tmp:
        for i, (scene, extra, secs, title, sub) in enumerate(FRAMES):
            p = Path(tmp) / f"f{i}.png"
            shoot(scene, p, 820, 1, extra)
            frames.append(caption_frame(p, title, sub))
            durations.append(int(secs * 1000))
            print("demo frame", i + 1, scene)
    pal = [f.quantize(colors=128, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE) for f in frames]
    out = OUT_DEMO / "wattsync-demo.gif"
    pal[0].save(out, save_all=True, append_images=pal[1:], duration=durations, loop=0, optimize=True, disposal=2)
    print("demo", out, round(out.stat().st_size / 1e6, 2), "MB")


if __name__ == "__main__":
    if not Path(CHROME).exists():
        sys.exit("Google Chrome not found at " + CHROME)
    what = sys.argv[1] if len(sys.argv) > 1 else "all"
    if what in ("all", "guide"):
        guide_shots()
    if what in ("all", "demo"):
        demo()
