"""Draws the diagrams in docs/diagrams/ as SVG files.  Run:  python3 docs/diagram-src/make_diagrams.py

Plain boxes and arrows with a light background, so they read the same in a browser, in IntelliJ and on GitHub.
"""
import os

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "diagrams")

INK, MUTED, LINE = "#111827", "#475569", "#94a3b8"
FILLS = {  # fill, stroke
    "box": ("#ffffff", LINE),
    "blue": ("#e0efff", "#0070f3"),
    "ok": ("#d1fae5", "#10b981"),
    "bad": ("#fee2e2", "#ef4444"),
    "ask": ("#fff7e0", "#e5c365"),
    "note": ("#f1f5f9", "#cbd5e1"),
}


def esc(text):
    return text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


class Svg:
    def __init__(self, width, height, title, description):
        self.w, self.h, self.title, self.description = width, height, title, description
        self.parts = []

    def add(self, markup):
        self.parts.append(markup)

    def text(self, cx, cy, lines, size=14, weight=400, fill=INK, anchor="middle"):
        if isinstance(lines, str):
            lines = [lines]
        lh = size * 1.3
        top = cy - (len(lines) - 1) * lh / 2
        spans = "".join(
            f'<tspan x="{cx}" y="{top + i * lh + size * 0.35:.1f}">{esc(line)}</tspan>' for i, line in enumerate(lines)
        )
        self.add(f'<text text-anchor="{anchor}" font-size="{size}" font-weight="{weight}" fill="{fill}">{spans}</text>')

    def box(self, x, y, w, h, lines, kind="box", size=14, weight=400, dashed=False):
        fill, stroke = FILLS[kind]
        dash = ' stroke-dasharray="6 4"' if dashed else ""
        self.add(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="10" fill="{fill}" stroke="{stroke}" stroke-width="1.5"{dash}/>')
        self.text(x + w / 2, y + h / 2, lines, size, weight)

    def pill(self, x, y, w, h, lines, kind="blue"):
        fill, stroke = FILLS[kind]
        self.add(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{h / 2}" fill="{fill}" stroke="{stroke}" stroke-width="1.5"/>')
        self.text(x + w / 2, y + h / 2, lines, 14, 600)

    def diamond(self, cx, cy, w, h, lines, kind="ask", size=13):
        fill, stroke = FILLS[kind]
        pts = f"{cx},{cy - h / 2} {cx + w / 2},{cy} {cx},{cy + h / 2} {cx - w / 2},{cy}"
        self.add(f'<polygon points="{pts}" fill="{fill}" stroke="{stroke}" stroke-width="1.5"/>')
        self.text(cx, cy, lines, size, 600)

    def arrow(self, points, label=None, at=None, both=False, dashed=False, anchor="middle", color=MUTED, head=True):
        pts = " ".join(f"{x},{y}" for x, y in points)
        dash = ' stroke-dasharray="6 4"' if dashed else ""
        start = ' marker-start="url(#head-start)"' if both else ""
        end = ' marker-end="url(#head)"' if head else ""
        self.add(f'<polyline points="{pts}" fill="none" stroke="{color}" stroke-width="1.8"{dash}{end}{start}/>')
        if label:
            x, y = at
            self.text(x, y, label, 12.5, 600, MUTED, anchor)

    def save(self, name):
        head = (
            f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {self.w} {self.h}" width="{self.w}" height="{self.h}" '
            f'role="img" aria-labelledby="t d" font-family="-apple-system, BlinkMacSystemFont, Segoe UI, Roboto, Helvetica, Arial, sans-serif">\n'
            f'<title id="t">{esc(self.title)}</title><desc id="d">{esc(self.description)}</desc>\n'
            f'<defs>'
            f'<marker id="head" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="8" markerHeight="8" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="{MUTED}"/></marker>'
            f'<marker id="head-start" viewBox="0 0 10 10" refX="1" refY="5" markerWidth="8" markerHeight="8" orient="auto-start-reverse"><path d="M10,0 L0,5 L10,10 z" fill="{MUTED}"/></marker>'
            f'</defs>\n<rect width="{self.w}" height="{self.h}" fill="#f8fafc"/>\n'
        )
        with open(os.path.join(OUT, name), "w", encoding="utf-8") as f:
            f.write(head + "\n".join(self.parts) + "\n</svg>\n")
        print("wrote", name)


# ---------------------------------------------------------------------------------------------------------------
def system_overview():
    s = Svg(1020, 470, "WattSync system overview",
            "The Envoy gateway is read every 60 seconds by the controller (a Raspberry Pi or an ESP32-S3, one at a time), "
            "which controls the wallbox over OCPP and serves a dashboard to a phone or computer.")
    s.add('<rect x="16" y="16" width="988" height="438" rx="14" fill="#ffffff" stroke="#e2e8f0" stroke-width="1.5" stroke-dasharray="3 4"/>')
    s.text(32, 38, "Home network (router and Wi-Fi)", 13, 600, MUTED, "start")

    s.box(40, 100, 220, 100, ["Enphase Envoy", "solar and grid meters"], "box", 15, 600)
    s.box(780, 100, 200, 100, ["Wallbox", "OCPP 1.6J charger", "plugged into the car"], "box", 15, 600)

    s.add('<rect x="400" y="62" width="240" height="268" rx="12" fill="#eef5ff" stroke="#0070f3" stroke-width="1.5"/>')
    s.text(520, 84, "WattSync controller", 15, 700, "#0b3d91")
    s.text(520, 102, "only one is connected to the wallbox", 12, 400, MUTED)
    s.box(420, 120, 200, 86, ["Raspberry Pi", "Java app", "dashboard and OCPP on :8080"], "box", 13, 600)
    s.box(420, 220, 200, 94, ["ESP32-S3", "C++ firmware", "dashboard :80, OCPP :8080", "setup network"], "box", 13, 600)

    s.arrow([(400, 150), (260, 150)], ["HTTPS read", "every 60 s (token)"], (330, 120), color=MUTED)
    s.arrow([(640, 150), (780, 150)], None, both=True)
    s.text(710, 122, ["OCPP over", "WebSocket"], 12.5, 600, MUTED)
    s.text(710, 180, ["start, limit, stop", "status, meter values"], 12, 400, MUTED)

    s.box(400, 370, 240, 64, ["Phone or computer", "dashboard in a browser"], "blue", 14, 600)
    s.arrow([(520, 370), (520, 330)], "HTTP", (548, 352), color="#0070f3")

    s.box(40, 330, 300, 104, ["Setup page (ESP32 only)", "join the Wi-Fi “WattSync-Setup”,", "open 192.168.4.1/setup", "to change Wi-Fi and Enphase details"], "note", 13, 400, dashed=True)
    s.arrow([(340, 382), (400, 382)], None, dashed=True)

    s.text(880, 250, ["Pi and ESP32 both", "do the same job; the", "dashboard page is shared"], 12.5, 400, MUTED)
    s.save("system-overview.svg")


# ---------------------------------------------------------------------------------------------------------------
def startup_flow():
    s = Svg(1060, 1060, "Start-up flow of the ESP32 and when the setup network opens",
            "The board loads its settings, opens the setup network when no Wi-Fi is set, tries to join, keeps a new network only if it works, "
            "and otherwise puts the old one back and opens the setup network.")
    cx, rx, trunk = 290, 790, 560   # main column, right column, and the line every retry returns along

    s.pill(cx - 130, 20, 260, 44, "Power on or restart", "blue")
    s.arrow([(cx, 64), (cx, 96)])
    s.box(cx - 190, 96, 380, 64, ["Load the settings: what was saved from", "the setup page, otherwise secrets.h"])
    s.arrow([(cx, 160), (cx, 196)])

    # --- no Wi-Fi set: setup network, wait, restart ---
    s.diamond(cx, 246, 300, 100, ["Is a Wi-Fi network set?"])
    s.arrow([(cx + 150, 246), (rx - 150, 246)], "No", (cx + 230, 232))
    s.box(rx - 150, 206, 300, 80, ["Open the setup network and", "keep it open (nothing else to do)"], "ask")
    s.arrow([(rx, 286), (rx, 330)])
    s.box(rx - 150, 330, 300, 56, ["Wait until a phone saves", "a network on the setup page"])
    s.arrow([(rx, 386), (rx, 424)])
    s.box(rx - 150, 424, 300, 44, "Restart the board", "box")
    s.arrow([(rx + 150, 446), (1030, 446), (1030, 128), (cx + 190, 128)])
    s.text(1022, 300, "start over", 12.5, 600, MUTED, "end")

    # --- try to join ---
    s.arrow([(cx, 296), (cx, 346)], "Yes", (cx + 24, 322), anchor="start")
    s.box(cx - 170, 346, 340, 64, ["Try to join the network", "(up to 30 seconds per try)"])
    s.arrow([(cx, 410), (cx, 456)])
    s.diamond(cx, 506, 240, 100, ["Joined?"])

    s.arrow([(cx, 556), (cx, 620)], "Yes", (cx + 24, 590), anchor="start")
    s.box(cx - 190, 620, 380, 78, ["If it was a trial of a new network, keep it.", "Carry on starting: clock, dashboard,", "OCPP server, meter polling"], "ok")
    s.arrow([(cx, 698), (cx, 730)])
    s.pill(cx - 130, 730, 260, 44, "Running normally", "ok")

    # --- not joined ---
    s.arrow([(cx + 120, 506), (rx, 506), (rx, 545)], "No", (cx + 195, 492))
    s.diamond(rx, 600, 340, 110, ["Was this a trial of a network", "just chosen on the setup page?"], "ask", 12.5)

    s.arrow([(rx, 655), (rx, 690)], "Yes", (rx + 22, 676), anchor="start")
    s.box(rx - 180, 690, 360, 100, ["Put the previous network back,", "remember why it failed and open the", "setup network for 15 minutes.", "Then try the old network again."], "bad", 13)
    s.arrow([(rx - 180, 740), (trunk, 740)], None, head=False)

    s.arrow([(rx + 170, 600), (1010, 600), (1010, 890), (rx + 170, 890)], "No", (1022, 760), anchor="start")
    s.diamond(rx, 890, 340, 110, ["Failing for 3 minutes", "since start-up?"], "ask", 12.5)
    s.arrow([(rx - 170, 890), (trunk, 890)], None, head=False)
    s.text(trunk + 8, 862, ["No: wait 5 s,", "then try again"], 12.5, 600, MUTED, "start")

    s.arrow([(rx, 945), (rx, 975)], "Yes", (rx + 22, 962), anchor="start")
    s.box(rx - 200, 975, 400, 62, ["Open the setup network once, for 15 minutes.", "Keep retrying every minute while nobody is on it."], "ask", 13)
    s.arrow([(rx - 200, 1006), (trunk, 1006)], None, head=False)

    # --- the one line every retry returns along, into "Try to join" ---
    s.arrow([(trunk, 1006), (trunk, 378), (cx + 170, 378)], "try again", (trunk + 8, 440), anchor="start")

    s.text(cx - 190, 860, ["The setup network also opens by hand:", "hold BOOT for 5 seconds, or type “setup”."], 12.5, 400, MUTED, "start")
    s.save("setup-startup-flow.svg")


# ---------------------------------------------------------------------------------------------------------------
def wifi_trial():
    s = Svg(1060, 430, "A Wi-Fi change is a trial",
            "Saving a different network keeps the previous one in reserve. If the board joins the new network it keeps it; "
            "if not, the previous network is put back and the setup page shows the reason.")
    s.text(30, 30, "Changes to the token, Envoy address and update password are kept either way.", 12.5, 400, MUTED, "start")

    s.box(30, 140, 200, 100, ["Working network A", "saved and in use"], "ok", 15, 600)
    s.arrow([(230, 190), (390, 190)], ["Save network B", "on the setup page"], (310, 158))
    s.box(390, 120, 250, 140, ["Trial of network B", "A is remembered as the", "previous network.", "The board restarts and tries B."], "ask", 14, 600)

    s.arrow([(640, 150), (720, 95), (800, 95)], "joined", (700, 72))
    s.box(800, 55, 230, 80, ["Working network B", "A is forgotten"], "ok", 15, 600)

    s.arrow([(640, 235), (720, 300), (800, 300)])
    s.text(720, 336, ["could not join", "(wrong password,", "5 GHz only, out of range)"], 12.5, 600, MUTED)
    s.box(800, 255, 230, 90, ["Back on network A", "the setup network opens and", "the page says why"], "bad", 14, 600)

    s.arrow([(915, 345), (915, 380), (130, 380), (130, 240)], None, dashed=True)
    s.text(520, 404, "The old network is only dropped after the new one has worked, so a typo can never leave the board without Wi-Fi.", 12.5, 600, MUTED)
    s.save("wifi-trial.svg")


# ---------------------------------------------------------------------------------------------------------------
def settings_precedence():
    s = Svg(1020, 400, "Where each setting comes from",
            "secrets.h holds the defaults compiled into the firmware. Whatever was saved from the setup page is kept in flash and wins, "
            "field by field. An empty secret field on the page keeps the saved value.")
    s.box(30, 40, 300, 170, ["secrets.h (compiled in)", "the defaults", "", "Wi-Fi name and password", "Enphase token", "Envoy address", "Update password"], "note", 14, 400)
    s.box(30, 250, 300, 90, ["Setup page (WattSync-Setup)", "writes what you enter to the", "board’s flash (not to secrets.h)"], "blue", 14, 600)

    s.box(370, 150, 190, 60, ["for each field,", "which one is set?"], "ask", 14, 600)
    s.arrow([(330, 125), (465, 125), (465, 150)], "defaults", (400, 108))
    s.arrow([(330, 295), (465, 295), (465, 210)])
    s.text(400, 318, ["saved in flash:", "this one wins"], 12.5, 600, MUTED)

    s.arrow([(560, 180), (650, 180)])
    s.add('<rect x="650" y="30" width="350" height="320" rx="10" fill="#d1fae5" stroke="#10b981" stroke-width="1.5"/>')
    s.text(825, 56, "Values in use", 15, 700)
    bullets = [
        "• A field saved from the page wins,",
        "   otherwise the secrets.h value.",
        "• The Wi-Fi name and its password",
        "   go together.",
        "• An Envoy address saved as empty means",
        "   “find it automatically”, even if",
        "   secrets.h has an address.",
        "• An empty secret field on the page",
        "   keeps what is saved.",
        "• No update password anywhere:",
        "   updating over Wi-Fi stays off.",
    ]
    s.text(668, 205, bullets, 13.5, 400, INK, "start")
    s.text(30, 380, "“setup clear” in the serial console forgets everything saved from the page, so secrets.h applies again.", 12.5, 600, MUTED, "start")
    s.save("settings-precedence.svg")


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    system_overview()
    startup_flow()
    wifi_trial()
    settings_precedence()
