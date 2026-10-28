#!/usr/bin/env python3
"""Builds docs/demo/wattsync-tesla-pitch.mp4: a short phone-sized video for asking a Tesla owner to volunteer as a tester.

Like make_demo.py, every picture is the REAL dashboard page; the numbers are made up and the "board" is the stand-in in
car_standin.py (a paired car, Tesla mode on). Nothing here talks to a real board or car.
Run from the repository root:   FFMPEG=/path/to/ffmpeg python3 docs/demo-src/make_neighbor_demo.py
Needs: Google Chrome (macOS app), Pillow, and ffmpeg (from $FFMPEG, the PATH, or `pip install imageio-ffmpeg`).
"""
import subprocess
import sys
import tempfile
import time
import urllib.parse
import urllib.request
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import make_demo as md                      # noqa: E402
from PIL import Image, ImageDraw           # noqa: E402

ROOT = md.ROOT
OUT = md.OUT_DEMO / "wattsync-tesla-pitch.mp4"
PORT = 8099
BASE = f"http://127.0.0.1:{PORT}"
K = 2                                       # pixel scale (the phone is 402 css px wide)
NAVY, WHITE, SOFT, BLUE = (15, 23, 42), (255, 255, 255), (203, 213, 225), (96, 165, 250)


def fake(**state):
    urllib.request.urlopen(f"{BASE}/fake/set?" + urllib.parse.urlencode(state), timeout=10).read()


def shoot_page(query, path, height):
    """Screenshot of the stand-in's page in a 402 px wide frame served by the stand-in itself (same origin, so the page's own timers run)."""
    url = f"{BASE}/frame?h={height}&q={urllib.parse.quote(query.lstrip('?'))}"
    with tempfile.TemporaryDirectory() as tmp:
        raw = Path(tmp) / "raw.png"
        subprocess.run([md.CHROME, "--headless=new", "--disable-gpu", "--hide-scrollbars", f"--window-size=600,{height}", f"--force-device-scale-factor={K}",
                        "--virtual-time-budget=9000", f"--screenshot={raw}", url], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=90)
        Image.open(raw).crop((0, 0, md.WIDTH * K, height * K)).save(path)


def wrap(draw, text, font, width):
    lines, cur = [], ""
    for word in text.split():
        trial = (cur + " " + word).strip()
        if draw.textlength(trial, font=font) > width and cur:
            lines.append(cur)
            cur = word
        else:
            cur = trial
    return lines + [cur]


def card(title, items, height=820, note=None):
    """A plain text slide the size of a phone screenshot: a heading and a few short points."""
    w, h = md.WIDTH * K, height * K
    img = Image.new("RGB", (w, h), NAVY)
    d = ImageDraw.Draw(img)
    pad = 30 * K
    d.text((pad, 44 * K), title, font=md.font(27 * K, True), fill=WHITE)
    y = 100 * K
    f = md.font(17 * K)
    for item in items:
        lines = wrap(d, item, f, w - 2 * pad - 22 * K)
        d.ellipse((pad, y + 8 * K, pad + 8 * K, y + 16 * K), fill=BLUE)
        for line in lines:
            d.text((pad + 22 * K, y), line, font=f, fill=SOFT)
            y += 25 * K
        y += 14 * K
    if note:
        d.text((pad, h - 40 * K), note, font=md.font(13 * K), fill=(148, 163, 184))
    return img


def title_card():
    w, h = md.WIDTH * K, 820 * K
    img = Image.new("RGB", (w, h), NAVY)
    d = ImageDraw.Draw(img)
    d.text((30 * K, 250 * K), "WattSync", font=md.font(46 * K, True), fill=WHITE)
    y = 322 * K
    for line in ["Charge your Tesla", "from your own solar,", "with a small box and", "no Tesla account."]:
        d.text((30 * K, y), line, font=md.font(24 * K), fill=SOFT)
        y += 34 * K
    d.text((30 * K, 520 * K), "A 1 minute look, then a favour to ask.", font=md.font(16 * K), fill=BLUE)
    return img


def with_caption(path, head, sub):
    return md.caption_frame(path, head, sub, K)


def pad_even(img):
    w, h = img.size
    if w % 2 or h % 2:
        out = Image.new("RGB", (w + w % 2, h + h % 2), NAVY)
        out.paste(img, (0, 0))
        return out
    return img


def build():
    frames = []   # (image, seconds)
    server = subprocess.Popen([sys.executable, str(ROOT / "docs/demo-src/car_standin.py")], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        for _ in range(40):
            try:
                urllib.request.urlopen(f"{BASE}/favicon.ico", timeout=1)
            except urllib.error.HTTPError:
                break
            except Exception:
                time.sleep(0.25)
        fake(supported=1, paired=1, modeOn=1, through=1, reading=1, amps=16, charging=5, clock=1, meter="ok")
        with tempfile.TemporaryDirectory() as tmp:
            p = lambda n: Path(tmp) / f"{n}.png"
            # 1 title
            frames.append((title_card(), 4.0))
            # 2 the dashboard (sample numbers)
            md.shoot("charging", p("top"), 820 - 90, K, {})
            frames.append((with_caption(p("top"), "Your charging, on your phone", "Sample numbers. It shows what is happening and why, and you can pause it with one tap."), 5.0))
            # 3 the Tesla tile
            shoot_page("?scrollY=690", p("tile"), 820 - 90)
            frames.append((with_caption(p("tile"), "Your Tesla at a glance", "Battery, charge limit and the current it is charging at, read over Bluetooth when you are near the car."), 5.5))
            # 4 pick the current
            md.shoot("custom", p("custom"), 820 - 90, K, dict(scrollTo="#modes"))
            frames.append((with_caption(p("custom"), "You choose the charging current", "6 to 40 amps. With a solar meter it can also follow the spare sun on its own."), 5.0))
            # 5 settings: Tesla mode
            shoot_page("?settings=1&card=%23sec-tesla", p("set"), 820 - 90)
            frames.append((with_caption(p("set"), "One switch: Tesla mode", "Off by default. Bluetooth is never started until you turn it on, and it has its own Wi-Fi for the phone."), 5.5))
            # 6 safe and in your control
            frames.append((card("Safe, and in your hands", [
                "No Tesla account, no password and no cloud service: the box talks to the car directly over Bluetooth.",
                "You approve it once by tapping your key card on the car, the same as adding a phone key.",
                "You can remove it any time in the car: Controls > Locks.",
                "It never wakes the car unless you allow it.",
            ], 820 - 0), 8.0))
            # 7 what I would ask
            frames.append((card("What I'd ask of you", [
                "About 30 minutes, once, in your driveway with the car plugged in.",
                "You tap your key card to approve it. I change the charging current a few times and watch what the car does.",
                "Afterwards I remove the key if you like.",
                "It is an early test: so far only one Model Y. It should work with Model 3, Model Y and 2021 or newer Model S and X.",
            ], 820, note="Pictures use made-up sample numbers."), 9.0))
            frames.append((card("Up for it?", ["Just reply and tell me when you are around.", "No is a perfectly good answer."], 820), 4.0))
            ff = md.find_ffmpeg()
            if not ff:
                sys.exit("no ffmpeg: set $FFMPEG or pip install imageio-ffmpeg")
            # every slide the same size (a phone-shaped canvas), then each shown for its seconds at 2 pictures per second:
            # simple and exact, unlike a concat list with durations
            W = max(i.width for i, _ in frames)
            H = max(i.height for i, _ in frames)
            W, H = W + W % 2, H + H % 2
            n = 0
            for img, secs in frames:
                canvas = Image.new("RGB", (W, H), NAVY)
                canvas.paste(img.convert("RGB"), (0, 0))
                for _ in range(int(round(secs * 2))):
                    canvas.save(Path(tmp) / f"s{n:04d}.png")
                    n += 1
            md.OUT_DEMO.mkdir(parents=True, exist_ok=True)
            subprocess.run([ff, "-y", "-loglevel", "error", "-framerate", "2", "-i", str(Path(tmp) / "s%04d.png"), "-vf", "fps=30,format=yuv420p",
                            "-c:v", "libx264", "-crf", "22", "-movflags", "+faststart", str(OUT)], check=True)
            total = sum(s for _, s in frames)
            print("wrote", OUT, round(OUT.stat().st_size / 1e6, 2), "MB,", round(total), "s")
    finally:
        server.terminate()


if __name__ == "__main__":
    if not Path(md.CHROME).exists():
        sys.exit("Google Chrome not found at " + md.CHROME)
    build()
