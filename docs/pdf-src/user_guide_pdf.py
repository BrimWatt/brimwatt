"""Builds docs/user-guide.pdf: a plain-words guide for the person who owns the car and uses the dashboard.

Pictures come from docs/guide/*.png (make them with `python3 docs/demo-src/make_demo.py guide`). They are the real
dashboard page running on made-up sample numbers, not photos of a real house.
Run:  python3 docs/pdf-src/user_guide_pdf.py
"""
import io
import os
import sys

from PIL import Image
from reportlab.lib import colors
from reportlab.lib.pagesizes import letter
from reportlab.lib.styles import ParagraphStyle, getSampleStyleSheet
from reportlab.lib.units import inch
from reportlab.lib.utils import ImageReader
from reportlab.platypus import (Image as RLImage, KeepTogether, PageBreak, Paragraph, SimpleDocTemplate, Spacer, Table,
                                TableStyle)

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "user-guide.pdf")
SHOTS = os.path.join(HERE, "..", "guide")

sys.path.insert(0, os.path.join(HERE, "..", "demo-src"))
from make_demo import NOTES  # noqa: E402  the numbered callouts drawn on the pictures, with their explanations

ss = getSampleStyleSheet()
BLUE = colors.HexColor("#0b3d91")
body = ParagraphStyle("body", parent=ss["Normal"], fontSize=10.5, leading=15, spaceAfter=6)
h1 = ParagraphStyle("h1", parent=ss["Title"], fontSize=26, leading=30, textColor=BLUE, spaceAfter=4, alignment=0)
sub = ParagraphStyle("sub", parent=body, fontSize=12, textColor=colors.HexColor("#555555"), spaceAfter=14)
h2 = ParagraphStyle("h2", parent=ss["Heading2"], fontSize=15, leading=19, textColor=BLUE, spaceBefore=14, spaceAfter=6, keepWithNext=1)
h3 = ParagraphStyle("h3", parent=ss["Heading3"], fontSize=11.5, leading=15, spaceBefore=8, spaceAfter=3, keepWithNext=1)
small = ParagraphStyle("small", parent=body, fontSize=9, leading=12, textColor=colors.HexColor("#555555"), spaceAfter=2)
cell = ParagraphStyle("cell", parent=body, fontSize=9.5, leading=13, spaceAfter=0)
bullet = ParagraphStyle("bullet", parent=body, leftIndent=14, bulletIndent=2, spaceAfter=3)


def P(text, style=body):
    return Paragraph(text, style)


def bullets(items):
    return [Paragraph(t, bullet, bulletText="•") for t in items]


def shot(name, width=2.35 * inch, crop=None, caption=None):
    """A phone picture, optionally cut to (top, bottom) fractions of its height."""
    img = Image.open(os.path.join(SHOTS, name + ".png")).convert("RGB")
    if crop:
        img = img.crop((0, int(img.height * crop[0]), img.width, int(img.height * crop[1])))
    buf = io.BytesIO()
    img.save(buf, "JPEG", quality=88)
    buf.seek(0)
    ratio = img.height / img.width
    pic = RLImage(buf, width=width, height=width * ratio)
    pic.hAlign = "CENTER"
    key = [[P("<font color='#e11d48'><b>%d</b></font>&nbsp; <b>%s.</b> %s" % (i + 1, short.replace("&", "&amp;"), long.replace("&", "&amp;")), small)]
           for i, (_sel, short, long) in enumerate(NOTES.get(name, []))]
    t = Table([[pic]] + ([[P(caption, small)]] if caption else []) + key, colWidths=[width + 8])
    t.setStyle(TableStyle([("BOX", (0, 0), (0, 0), 0.6, colors.HexColor("#cfd6df")), ("LEFTPADDING", (0, 0), (-1, -1), 3),
                           ("RIGHTPADDING", (0, 0), (-1, -1), 3), ("TOPPADDING", (0, 0), (0, 0), 3), ("BOTTOMPADDING", (0, 0), (0, 0), 3),
                           ("BOX", (0, 1), (0, 1), 0, colors.white)]))
    return t


def side(pic, content, pic_left=False):
    """Text on one side, a phone picture on the other."""
    cols = [2.5 * inch, 4.3 * inch] if pic_left else [4.3 * inch, 2.5 * inch]
    row = [pic, content] if pic_left else [content, pic]
    t = Table([row], colWidths=cols)
    t.setStyle(TableStyle([("VALIGN", (0, 0), (-1, -1), "TOP"), ("LEFTPADDING", (0, 0), (-1, -1), 0), ("RIGHTPADDING", (0, 0), (-1, -1), 6)]))
    return t


def table(head, rows, widths):
    data = [[Paragraph("<font color='white'><b>%s</b></font>" % h, cell) for h in head]]
    data += [[Paragraph(c, cell) for c in r] for r in rows]
    t = Table(data, colWidths=widths, repeatRows=1)
    t.setStyle(TableStyle([("BACKGROUND", (0, 0), (-1, 0), BLUE), ("VALIGN", (0, 0), (-1, -1), "TOP"),
                           ("GRID", (0, 0), (-1, -1), 0.4, colors.HexColor("#cfd6df")),
                           ("ROWBACKGROUNDS", (0, 1), (-1, -1), [colors.white, colors.HexColor("#f8fafc")]),
                           ("TOPPADDING", (0, 0), (-1, -1), 4), ("BOTTOMPADDING", (0, 0), (-1, -1), 5)]))
    return t


def note(text):
    t = Table([[P(text, small)]], colWidths=[6.8 * inch])
    t.setStyle(TableStyle([("BACKGROUND", (0, 0), (-1, -1), colors.HexColor("#fff7e0")), ("BOX", (0, 0), (-1, -1), 0.5, colors.HexColor("#e5c365")),
                           ("TOPPADDING", (0, 0), (-1, -1), 6), ("BOTTOMPADDING", (0, 0), (-1, -1), 6)]))
    return t


def footer(canvas, doc):
    canvas.saveState()
    canvas.setFont("Helvetica", 8)
    canvas.setFillColor(colors.HexColor("#777777"))
    canvas.drawString(0.85 * inch, 0.5 * inch, "WattSync user guide. Pictures use made-up sample numbers.")
    canvas.drawRightString(letter[0] - 0.85 * inch, 0.5 * inch, "Page %d" % doc.page)
    canvas.restoreState()


S = []

# ---- cover and first look -------------------------------------------------------------------------------------------
S += [P("WattSync", h1), P("User guide: charge your car from your own sunshine", sub)]
S.append(side(shot("top", caption="Charging from the sun (sample data)"), [
    P("<b>What it does.</b> WattSync watches how much power your solar panels make and how much your house uses. It then tells your car charger how fast to charge, "
      "so the car drinks the spare sunshine instead of buying it from the power company."),
    P("<b>What you do.</b> Open the dashboard on your phone, pick a way to charge, and leave it. You can pause any time, set a goal for the morning, and see how much the sun saved you."),
    P("<b>Where it lives.</b> A small controller in your home (a Raspberry Pi or an ESP32 board) talks to your solar gateway and to your charger. The dashboard is a web page it serves to your phone. "
      "Open <font face='Courier'>http://wattsync.local</font> (the board) or <font face='Courier'>http://wattsyncpi.local:8080</font> (the Pi)."),
    P("<b>Tip.</b> On an iPhone, open the page in Safari, tap Share, then Add to Home Screen. It then opens like an app, and pulling down at the top refreshes it."),
]))

# ---- reading the dashboard ------------------------------------------------------------------------------------------
S += [P("1. Reading the dashboard", h2)]
S.append(side(shot("top-goal", caption="Top of the page"), [
    P("<b>The headline</b> says what is happening right now, in words: <i>Charging at 30 A</i>, <i>Not charging: Outside solar hours</i>, <i>Paused by you</i>. "
      "When it is not charging, it also says why. The red <b>Pause</b> button is next to it."),
    P("<b>The power flow</b> shows four boxes. <b>Solar</b> is what your panels make. <b>Home</b> is what the house uses without the car. "
      "<b>Car</b> is what the charger is drawing. <b>Grid</b> is power bought from, or sent to, the power company (green with an up arrow means you are sending power out)."),
    P("<b>What the charging gave you</b> is the card right under the headline: the energy the car took (or estimated miles of range) and <b>Saved</b> "
      "(money saved by using the sun instead of the grid), with a switch for <b>Today</b>, <b>Week</b>, <b>Month</b> and <b>Year</b>, a bar for the share that came "
      "from your panels, and a bar for each day (tap a day to see its curve in the graph). The <b>Miles</b> / <b>kWh</b> button flips the unit. "
      "When a charge finishes, a short green card says what was added, the solar share and the saving."),
    P("<b>Live pill.</b> The small pill at the top says <i>Live</i>. If it turns amber and says something like <i>Meter 12m old</i>, the solar readings are getting stale."),
], pic_left=True))

# ---- modes ----------------------------------------------------------------------------------------------------------
S += [P("2. Choose how to charge", h2),
      P("Four modes sit under the numbers. Tap one. The line below the buttons describes it in a sentence.")]
S.append(side(shot("modes-eco", width=2.0 * inch, caption="Eco, with the grid allowance"), [
    P("<b>Eco</b> charges only with spare solar: what the panels make, minus what the house is using. It is the only mode that takes the house into account. "
      "Charging needs at least 6 amps and starts once there is room for about 7 amps."),
    P("<b>Grid allowance</b> (the slider that appears in Eco and Max solar) is how much power from the grid the car may add to the sun. "
      "Zero means sun only. The default is 1000 W. Moving the slider sends the new value a few seconds after you stop."),
    P("<b>Max solar</b> uses everything the panels make and ignores the house. The grid allowance only tops the car up to the next whole amp, or to 6 amps when it is close."),
    P("<b>Custom</b> charges at a fixed current you choose, from 6 to 40 amps, whether the sun is out or not."),
    P("<b>Cheapest hours</b> is for power plans that cost more at certain hours (time of use). It charges at your maximum current whenever power is not expensive and stops during the expensive hours, "
      "with or without solar panels. Set the expensive hours (for example 4 pm to 9 pm) and the <i>Also on weekends</i> switch in Settings, under Money, with <i>Higher price at certain hours</i>. "
      "The headline then says <i>Waiting for cheaper hours</i> and when charging starts. A <i>Ready by</i> goal still takes over if time runs short."),
], pic_left=True))
S.append(side(shot("cheapest", width=2.0 * inch, caption="Cheapest hours (sample data)"), [
    P("With <b>Cheapest hours</b> selected, a panel under the buttons shows the price now, when it changes, and the hours it will not charge. <i>Change the hours</i> opens Settings, under Money. "
      "In the expensive hours the headline says <i>Waiting for cheaper hours</i> and when charging starts."),
    P("<b>No solar meter?</b> Then there is no power picture to show. The page shows price tiles instead (price now, when the peak starts or ends, and the car), and the solar share and CO2 lines are left out."),
    shot("no-solar", width=2.0 * inch, caption="A home with no solar meter (sample data)"),
], pic_left=True))
S.append(side(shot("impact", width=2.1 * inch, caption="Your impact (sample data)"), [
    P("<b>Your impact</b> opens from the small score button under the headline (for example <i>Solar Score 81 B</i>), as a sheet with Week and Month tabs. It gives a score out of 100 and a letter. "
      "With solar, half the points come from how much of the car's energy came from your panels, 30 from charging outside your expensive hours and 20 from Ready by goals met."),
    P("Without solar it is called <b>Charge Score</b> and counts only the parts that apply. Under the score you see miles, how much cheaper than gas, what Cheapest hours saved (an estimate), a streak of good days, badges, and a "
      "<i>Share image</i> button that makes a picture on your phone."),
    P("Days with no charging, or too little sun, are skipped and never count against you. Every figure is an estimate."),
    shot("impact-nosolar", width=2.1 * inch, caption="Charge Score, a home without solar"),
], pic_left=False))
S.append(side(shot("settings-money", width=2.4 * inch, crop=(0.28, 0.66), caption="Settings: Money, with your plan"), [
    P("Under <b>Money</b>, turn on <i>Higher price at certain hours</i>. A <b>Your plan</b> list fills in the peak hours for PG&amp;E E-TOU-C or SCE TOU-D-4-9PM (hours only, as published in October 2026; check your bill). "
      "The prices change, so enter yours. Tap the check mark to save the hours."),
], pic_left=True))
S.append(side(shot("modes-custom", width=2.0 * inch, caption="Custom: a fixed current"), [
    P("In Custom, a <b>Current</b> slider sets the speed. It ignores the sun, the grid allowance, the solar hours and the lost-meter safety stop, so it can use grid power day or night. "
      "Use it when you simply need a fast charge."),
    P("<b>Pause</b> is a kill switch that beats every mode. It stops a running charge, blocks any new start and any change of speed (even if someone starts the car from the charger's own app), "
      "and it stays on after a restart. Tap <b>Resume</b> to hand control back to the mode you had."),
], pic_left=False))

S.append(P("When charging starts and stops", h3))
S += bullets([
    "<b>Starts</b> when there has been enough spare power for about 3 minutes, and at least 2 minutes after the last stop.",
    "<b>Stops</b> when the spare power has stayed under the minimum for about 5 minutes. During that time the car keeps charging at the minimum, so a passing cloud does not stop it.",
    "<b>Speed changes</b> need a real difference (up by 2 amps for a minute, down by 1 amp for half a minute), so the charger is not nudged all the time.",
    "<b>Solar hours.</b> Eco and Max solar run from 30 minutes after sunrise to 1 hour before sunset. You can change both margins in Settings. Outside those hours the car is left alone.",
    "<b>Safety stop.</b> If the controller cannot read your solar meter for about 2.5 minutes, Eco and Max solar stop charging rather than guess.",
])

# ---- paused / unplugged / night -------------------------------------------------------------------------------------
S += [P("3. What the headline tells you when nothing is charging", h2)]
trio = Table([[shot("top-paused", width=2.0 * inch, caption="Paused by you"), shot("top-unplugged", width=2.0 * inch, caption="No car plugged in"),
               shot("top-night", width=2.0 * inch, caption="Outside solar hours")]], colWidths=[2.27 * inch] * 3)
trio.setStyle(TableStyle([("VALIGN", (0, 0), (-1, -1), "TOP"), ("LEFTPADDING", (0, 0), (-1, -1), 0)]))
S.append(trio)
S.append(Spacer(1, 6))
S.append(table(["You see", "It means", "What to do"], [
    ["Paused by you", "You pressed Pause. Nothing will start.", "Tap Resume when you want charging back."],
    ["No car plugged in", "Charging starts by itself once one is plugged in.", "Plug in the car."],
    ["Outside solar hours", "It is too early or too late in the day for Eco or Max solar.", "Wait for the morning, or use Custom to charge now."],
    ["Waiting: home is using the solar", "The solar is on, but the house is using it. The line under it shows the numbers and when it can start.", "Nothing; it starts when there is spare power. Or try Max solar."],
    ["Waiting for more solar", "Panels are not making enough for the minimum charge.", "Wait for brighter sun, or raise the grid allowance."],
    ["Starting soon / Waiting to restart", "There is enough solar and it is making sure it lasts, or it has just stopped and must pause for 2 minutes.", "Nothing; the countdown shows how long."],
    ["Low solar, charging at 6 A", "A cloud or a big load: it keeps going on the minimum for a few minutes before stopping.", "Nothing; it stops by itself if the dip lasts."],
    ["Lost contact with the solar meter: charging stopped", "The controller cannot read your solar gateway.", "Check the gateway is on and on the same Wi-Fi."],
    ["Charger is offline", "The controller cannot reach the charger.", "Check the charger is powered and on the network."],
], [1.9 * inch, 2.5 * inch, 2.4 * inch]))

# ---- goal -----------------------------------------------------------------------------------------------------------
S += [P("4. Ready by (departure goal)", h2)]
S.append(side(shot("settings-goal", width=2.4 * inch, crop=(0.25, 0.46), caption="Settings: Ready by"), [
    P("Open <b>Settings</b> (the gear icon at the top right) and the <b>Ready by</b> card. Turn on <i>Have the car ready at a set time</i>, then say how much energy you want, and by when, for example 20 kWh by 7:00 am or 5:30 pm. Tap the green check mark to save. Turn the switch off to stop."),
    P("Until time gets short, nothing changes: the sun charges as usual. When the time left is only just enough, WattSync charges at the lowest speed that still gets there, "
      "using grid power if it has to, even at night."),
    P("The headline then shows progress, for example <i>Goal: 12 of 20 kWh by 7:00 am</i>."),
    P("<b>Good to know.</b> It only works while a car is plugged in. It is ignored while paused and in Custom. It never goes above your maximum current. "
      "It counts energy delivered in the 16 hours before the deadline."),
], pic_left=True))

# ---- history --------------------------------------------------------------------------------------------------------
S += [P("5. History: what the sun did for you", h2)]
S.append(side(shot("graph-day", width=2.3 * inch, caption="Today's graph"), [
    P("Under the modes, <b>Today</b> shows a graph of the day: <font color='#b07a00'><b>solar</b></font> (yellow), <font color='#1f5fb8'><b>car</b></font> (blue), "
      "<b>home</b> (grey) and power bought from the <font color='#c0392b'><b>grid</b></font> (red). The strip under it shows when the car was charging. "
      "Touch the graph to read the numbers at that moment."),
    P("<b>7 days</b>, <b>30 days</b> and <b>Year</b> switch to bars, one per day (or month). Yellow is the part that came from the sun, red the part from the grid. "
      "Tap a day to see that day's curve."),
    P("<b>Activity</b> is a short list of what the controller did and when. <b>Charging sessions</b> lists each charge with when it ran, how much energy, "
      "how much came from the sun, the average current and what you saved. Both show 5 per page."),
], pic_left=True))
S.append(side(shot("graph-7", width=2.3 * inch, caption="Last 7 days"), [
    P("<b>How savings are worked out.</b> The sun's share of the car's energy times the price you set (cents per kWh, default 30). "
      "Set your real price, and an optional peak price and hours (turn on <i>Higher price at certain hours</i>), in Settings under <b>Money</b>. It is an estimate of money not spent on the grid, not a bill."),
], pic_left=False))

# ---- miles ----------------------------------------------------------------------------------------------------------
S += [P("6. Show energy as miles", h2)]
S.append(side(shot("top-miles", width=2.35 * inch, caption="Miles view (estimate)"), [
    P("Prefer miles to kilowatt-hours? In <b>Settings</b>, open <b>Units</b> and set <b>Show energy as</b> to <b>Miles</b>. Enter <b>Miles per kWh</b> for your car "
      "(1 to 6; it starts at 3). Miles are an estimate: the energy times that number, shown with a <b>~</b>."),
    P("Use the figure your car really gets, counted from the wall. It is a little lower than the car's own figure because charging loses some energy on the way."),
    P("<b>What changes.</b> The value card says <i>Range added</i> (its button flips back to kWh); the 7-day, 30-day and Year totals say <i>range added</i>; each charging session shows range added; "
      "the Car box shows <i>mi/h</i> (miles of range per hour) instead of kW; and the goal is typed in miles, with a small note under it showing the whole kWh that is actually sent to the charger."),
    P("<b>What does not change.</b> Savings stay in dollars. The power flow still shows watts. The activity lines the controller writes still say kWh, and so do the CSV export and message text. "
      "The choice is kept on this phone only."),
], pic_left=True))
S.append(side(shot("settings-miles", width=2.4 * inch, crop=(0.25, 0.46), caption="Settings: Ready by in miles"), [
    P("Your miles per kWh also sets how the goal converts. A goal above zero is never rounded down to zero, so asking for 1 mile still asks for 1 kWh."),
], pic_left=False))

# ---- settings -------------------------------------------------------------------------------------------------------
S += [P("7. Settings, one by one", h2),
      P("Tap the gear icon at the top right. Everything is on one page, in cards; the names at the top (Charging, Money, Messages, Tesla, More) jump to a section. Number boxes show a green check mark when you change them: press it (or Enter) to save, Escape puts the saved value back.")]
S.append(table(["Setting", "What it does"], [
    ["Solar hours only", "Keeps Eco and Max solar to the daylight window. Switch off to allow them at any hour."],
    ["Start / Stop (minutes)", "How long after sunrise charging may start, and how long before sunset it stops. 0 to 180 minutes each."],
    ["Ready by", "A switch. On: the energy (or miles) you want and the time you want it by, any time of day. Off: no goal. See section 4."],
    ["Money", "Your price per kWh in cents, and a switch for an optional peak price with its hours. Only used to work out savings."],
    ["Limits", "Maximum current the charger is ever asked for, in any mode (6 to 40 A). Set it to what your wiring and car can take."],
    ["Display", "Theme (Match my phone, Light or Dark; it only changes how the page looks on this phone) and kWh or miles, with miles per kWh. See section 6."],
    ["Messages", "An optional web address (Slack, Discord, Home Assistant and similar). WattSync posts a short message when charging ends, the solar meter is lost or returns, the charger disconnects, or the goal takes over. "
                 "The same message is sent at most once per 10 minutes. Use <i>Send test</i> to check it. Only the address's host is shown afterwards."],
    ["Diagnostics", "A short health list: setup check (solar meter, charger, clock), how long it has been running, why it last restarted, free memory and Wi-Fi signal."],
], [1.7 * inch, 5.1 * inch]))
S.append(Spacer(1, 6))
S.append(shot("settings-diag", width=1.8 * inch, crop=(0.0, 1.0), caption="Settings with Diagnostics open (sample data)"))

# ---- help -----------------------------------------------------------------------------------------------------------
S += [P("8. Quick answers", h2)]
S.append(table(["Question", "Answer"], [
    ["It is sunny but the car is not charging.", "Read the headline; it says why. Often the house is using the sun, the minimum charge is not reachable yet, or the 3-minute wait has not passed."],
    ["Can I charge right now regardless of the sun?", "Yes: choose <b>Custom</b> and set the current. It uses grid power if needed."],
    ["I need the car full by a certain time.", "Use <b>Ready by</b>. It charges only as fast as needed to get there."],
    ["How do I stop everything quickly?", "Tap <b>Pause</b>. It survives restarts until you tap Resume."],
    ["The numbers look old.", "The pill turns amber (<i>Meter 12m old</i>) when daytime readings are stale. Check the solar gateway and Wi-Fi."],
    ["Can I use it away from home?", "Only on your home network, unless you set up your own remote access. The page has no sign-in, so do not expose it to the internet."],
], [2.4 * inch, 4.4 * inch]))
S.append(Spacer(1, 8))
S.append(note("<b>About the pictures.</b> Every screen in this guide is the real dashboard page showing made-up sample numbers; none comes from a real house. "
              "Miles are estimates from the number you enter. Savings are estimates based on your price setting."))


def build():
    doc = SimpleDocTemplate(OUT, pagesize=letter, leftMargin=0.85 * inch, rightMargin=0.85 * inch, topMargin=0.8 * inch, bottomMargin=0.8 * inch,
                            title="WattSync user guide", author="WattSync")
    doc.build(S, onFirstPage=footer, onLaterPages=footer)
    print("wrote", os.path.abspath(OUT), round(os.path.getsize(OUT) / 1e6, 2), "MB")


if __name__ == "__main__":
    build()
