from reportlab.lib.pagesizes import letter
from reportlab.lib import colors
from reportlab.lib.styles import getSampleStyleSheet, ParagraphStyle
from reportlab.lib.units import inch
from reportlab.platypus import SimpleDocTemplate, Paragraph, Spacer, Preformatted, Table, TableStyle, KeepTogether

import os
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "test-scenarios.pdf")
ss = getSampleStyleSheet()
body = ParagraphStyle("body", parent=ss["Normal"], fontSize=10, leading=14, spaceAfter=6)
h1 = ParagraphStyle("h1", parent=ss["Title"], fontSize=22, leading=26, textColor=colors.HexColor("#0b3d91"), spaceAfter=4)
sub = ParagraphStyle("sub", parent=body, textColor=colors.HexColor("#555555"), spaceAfter=12)
h2 = ParagraphStyle("h2", parent=ss["Heading2"], fontSize=13, leading=17, textColor=colors.HexColor("#0b3d91"),
                    spaceBefore=12, spaceAfter=4, keepWithNext=1)
cell = ParagraphStyle("cell", parent=body, fontSize=8.8, leading=11.5, spaceAfter=0)
head = ParagraphStyle("head", parent=cell, fontName="Helvetica-Bold", textColor=colors.white)
bold = ParagraphStyle("bold", parent=cell, fontName="Helvetica-Bold")
code = ParagraphStyle("code", fontName="Courier", fontSize=9, leading=12, leftIndent=8, rightIndent=8)
small = ParagraphStyle("small", parent=body, fontSize=9.5)

W = [0.4 * inch, 1.35 * inch, 2.0 * inch, 2.65 * inch, 0.5 * inch]

def code_block(text):
    t = Table([[Preformatted(text, code)]], colWidths=[sum(W)])
    t.setStyle(TableStyle([("BACKGROUND", (0, 0), (-1, -1), colors.HexColor("#f3f5f8")),
                           ("BOX", (0, 0), (-1, -1), 0.5, colors.HexColor("#cfd6df")),
                           ("TOPPADDING", (0, 0), (-1, -1), 6), ("BOTTOMPADDING", (0, 0), (-1, -1), 6)]))
    return t

_count = 0   # scenarios are numbered in order, whatever number each row was given

def section(title, rows):
    global _count
    data = [[Paragraph(x, head) for x in ("#", "Scenario", "Do", "Expect", "Pass")]]
    for _, name, do, expect, first in rows:
        _count += 1
        data.append([Paragraph(str(_count), bold), Paragraph(name + ("<br/><font color='#b45309'>run first</font>" if first else ""), bold),
                     Paragraph(do, cell), Paragraph(expect, cell), Paragraph("[ &nbsp; ]", cell)])
    t = Table(data, colWidths=W, repeatRows=1)
    t.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, 0), colors.HexColor("#0b3d91")),
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
        ("GRID", (0, 0), (-1, -1), 0.4, colors.HexColor("#cfd6df")),
        ("ROWBACKGROUNDS", (0, 1), (-1, -1), [colors.white, colors.HexColor("#f8fafc")]),
        ("TOPPADDING", (0, 0), (-1, -1), 4), ("BOTTOMPADDING", (0, 0), (-1, -1), 5),
    ]))
    return [Paragraph(title, h2), t]

S = []
S += [Paragraph("Test Scenarios for WattSync", h1),
      Paragraph("Checks to run on the real house to confirm the system behaves as expected. Tick the Pass box as you go.", sub),
      Paragraph("Before you start", h2),
      Paragraph("Follow the live log in one terminal while you test:", body),
      code_block("journalctl -u solar-charger -f"),
      Spacer(1, 6),
      Paragraph("Timings to keep in mind: the meters are read every <b>60 seconds</b>; ECO needs about <b>3 minutes</b> of confirmed "
                "surplus before it starts and, when the available current falls under its minimum (6 A in both ECO and Max solar), runs at that minimum and stops only after the shortfall has lasted <b>5 minutes</b> (so a short dip may import a little over the allowance); "
                "a change to the app's state is saved within <b>5 seconds</b>; the solar modes only run inside the "
                "<b>daylight window</b> (1 hour after sunrise to 1 hour before sunset). Scenarios marked <font color='#b45309'>run first</font> "
                "cover a real start, a real stop, a restart mid-charge, the gateway fail-safe, and the daylight window closing.", body)]

S += section("A. Basics (Custom mode: fixed current)", [
    (1, "Charger connects", "Power up the wallbox.",
     "Log: <font face='Courier'>Wallbox connected</font>, then <font face='Courier'>BootNotification</font>. Dashboard badge turns green.", False),
    (2, "Custom starts a charge", "Plug the car in. Choose Custom, move the slider to 10 A and wait 5 seconds.",
     "<font face='Courier'>RemoteStartTransaction (10A)</font>, then <font face='Courier'>answered ... Accepted</font>, then "
     "<font face='Courier'>status: CHARGING</font>. Compare the real draw with the car's own app or a meter.", True),
    (3, "Change current mid-charge", "Move the slider to 16 A while charging and wait 5 seconds.",
     "<font face='Courier'>SetChargingProfile (16A, txn ...)</font> and <font face='Courier'>Accepted</font>. Real current follows within a minute.", False),
    (4, "Slider does not spam the charger", "In Custom mode, drag the slider back and forth for about 10 seconds, then let go.",
     "While you move it, the text counts down (<i>Sending 20 A in 3 s</i>) and restarts on each move. Five seconds after you "
     "stop, exactly one <font face='Courier'>SetChargingProfile</font> appears in the log, and the toast says <i>Custom 20 A</i>.", True),
    (5, "Leaving Custom drops an unsent move", "Move the slider, then tap Eco within 5 seconds.",
     "Eco switches on and the toast says <i>Unsent change dropped</i>. No current command is sent.", False)])

S += section("B. ECO behavior", [
    (5, "Start on real surplus", "Switch to ECO on a sunny day with the car plugged in.",
     "<i>waiting for surplus</i>, then <i>surplus present, confirming it lasts</i>, then <i>start</i> after about 2 more minutes. "
     "Export drops towards the allowance.", True),
    (6, "No start without surplus", "Try in the evening, or with an oven on.",
     "Stays at <i>waiting for surplus</i>. The car does not start.", False),
    (7, "Short dip", "Turn on a big load for a couple of minutes, or wait for a cloud.",
     "A dip only lowers the limit towards the minimum (6 A in Eco). If available stays under 6 A for 5 minutes it stops (scenario 8); a dip that ends sooner never stops the charge.", False),
    (8, "Dip under the minimum", "Switch on enough load that the poll line shows <i>available</i> under 6 A, and keep it on for over 5 minutes.",
     "The limit drops to 6 A and the charge keeps going; after about 5 minutes: <i>stop: surplus below the minimum, would import more than the allowance</i> and "
     "<font face='Courier'>RemoteStopTransaction</font>. Grid import falls back to the house load. It does not restart for at least "
     "2 minutes, even if the sun returns.", True),
    (9, "No chatter on small swings", "Switch a 1 to 2 kW load on and off a few times.",
     "Few <font face='Courier'>SetChargingProfile</font> lines, each a step of 1 A or more down (2 A or more up) and 30 to 60 seconds apart.", False),
    ("9b", "Import never exceeds the allowance", "Watch grid import in ECO while charging, with the allowance at 0, 500 and 1000 W.",
     "Grid import stays at or under the allowance, apart from a poll or two of lag after a sudden load and the up-to-5-minute dips where the car runs at its minimum current. Anything longer is a bug: send the log.", False)])

S += section("C. Grid allowance", [
    (10, "Raise it", "Drag the allowance slider from 0 W up to 1000 W while ECO waits on a marginal surplus.",
     "Start comes sooner. <font face='Courier'>allowance 1000W</font> shows in the poll line.", False),
    (11, "Set it to 0", "Drag the slider to 0 and watch.",
     "ECO needs about 1.6 kW or more of real export to start, and the limit stops following grid power.", False)])

S += section("D. Max solar and mode switching", [
    (12, "Max solar ignores the house", "Switch to Max solar (the sun button) in daylight, with a big house load on (oven or dryer).",
     "It starts after about 3 minutes once solar plus the allowance can reach 6 A, and charges at roughly the solar output whatever the house uses (anything the house needs beyond the panels comes from the grid). With 1 kW of sun and a 600 W allowance it charges at 6 A; with 0 W allowance and the same sun it stays off. After the sun is gone for 5 minutes it stops.", False),
    (12.5, "Home load only matters in Eco", "In Eco switch on a big load, then do the same in Max solar.",
     "Eco lowers and stops (the headline says <i>Home is using the solar</i>); Max solar keeps following the panels and never mentions the house.", False),
    (13, "Switch while charging", "Switch Custom to ECO during a charge, then back.",
     "On the first reading it adjusts to what surplus supports, with no stop and start gap and no leftover value from the old mode.", False)])

S += section("E. Measured vs assumed current", [
    (14, "Read the label", "Look at the poll line during a charge.",
     "<font face='Courier'>charger 11.8A (measured)</font> means the wallbox reports its current. "
     "<font face='Courier'>(assumed from limit)</font> means it does not: compare the dashboard figure with the car's real draw.", False)])

S += section("F. Restarts and crashes", [
    (15, "Clean restart mid-charge", "Run <font face='Courier'>sudo systemctl restart solar-charger</font> while charging.",
     "<i>Restored last known state</i>. Dashboard shows <i>restored, verifying</i>, then Charging. Car keeps charging and limit changes still work with the same transaction.", True),
    (16, "Hard kill", "Run <font face='Courier'>sudo systemctl kill -s SIGKILL solar-charger</font>. systemd restarts it after 10 seconds.",
     "Same recovery as above, losing at most about 5 seconds of changes.", False),
    (17, "Restart in ECO, no surplus", "Restart in the evening while the car is charging.",
     "Steps down to 6 A, then stops after about 5 minutes. The car is not left charging unmanaged.", True),
    (18, "No state file", "Stop the app, delete <font face='Courier'>/home/wattsync/solar-charger-state.json</font>, start it.",
     "<i>No saved state ... starting with defaults</i>, ECO and 32 A. The file reappears within about 5 seconds.", False)])

S += section("G. Gateway problems", [
    (19, "Gateway unreachable", "Unplug the Envoy (power or network) for over 3 minutes while charging in ECO.",
     "One error line, then <font face='Courier'>[FAILSAFE]</font>. The car stops in both ECO and Max solar. Red banner and "
     "NO DATA on the dashboard. After reconnecting: <i>reachable again</i>, and ECO re-confirms surplus before starting.", True),
    (20, "Bad token", "Put a wrong value in <font face='Courier'>/etc/solar-charger.env</font> and restart. Restore it afterwards.",
     "<i>Enphase rejected the access token ... renew ENPHASE_TOKEN</i> in the log and on the dashboard.", False)])

S += section("H. Charger side", [
    (21, "Car unplugged mid-charge", "Unplug the car while charging.",
     "Status moves to Finishing or Available and the app stops sending limits. Plug back in: it starts again by the normal rules.", False),
    (22, "Wallbox power-cycled", "Switch the wallbox off and on.",
     "Disconnect and reconnect log lines, and <i>Restored last known state</i> if it returns within an hour.", False),
    (23, "Heartbeat and idle", "Leave the charger connected and idle for 10 minutes.",
     "No disconnects. A heartbeat roughly every minute.", False)])

S += section("I. Daylight window", [
    (24, "Window closes at dusk", "Leave ECO on with the car charging through the evening. Today's window is on the dashboard and in the startup log.",
     "At the closing time: <font face='Courier'>[DAYLIGHT] Outside the daylight window</font>, a stop command, no more "
     "<font face='Courier'>[ECO]</font> poll lines, and the dashboard shows <i>Outside the solar window</i> and <i>paused</i>.", True),
    (25, "Window opens in the morning", "Leave the car plugged in overnight in ECO.",
     "Nothing happens at night. At opening: <font face='Courier'>[DAYLIGHT] Window open</font>, polling resumes, and ECO "
     "starts by the normal rules once surplus is confirmed.", False),
    (26, "Turn the window off", "Untick <i>Solar hours only</i> in the evening.",
     "Polling and ECO carry on after sunset. The setting is still off after a restart. Remember to turn it back on.", False),
    (27, "Custom at night", "After the window has closed, choose Custom and set a current with the slider.",
     "Charging starts and the limit follows what you set. The window has no effect on Custom mode.", False),
    (28, "Restart overnight", "Run <font face='Courier'>sudo systemctl restart solar-charger</font> while the window is closed.",
     "The startup log shows today's window, there is no polling, and nothing is sent to a charger that is not charging.", False)])

S += section("J. Dashboard feedback", [
    (29, "Every action confirms", "Switch modes, move the Custom slider, drag the allowance slider, tick and untick Solar hours only.",
     "A short toast appears for each, with a second line only when needed (for example <i>Saved, charger offline</i>). The allowance slider sends only when released.", False),
    (30, "Bad values are refused", "Run <font face='Courier'>curl -X POST -H 'X-WattSync: 1' 'http://wattsyncpi.local:8080/api/allowance?watts=5000'</font> (the slider stops at 1000 W).",
     "The server answers 400 with the allowed range (0 to 1000 W). The current allowance is unchanged.", False),
    (31, "Controller goes away", "Stop the service, wait 10 seconds, then try a mode change. Start the service again.",
     "Pill shows <i>Offline</i> and a banner says it cannot reach the controller. The mode change shows an error toast and the old mode stays selected. After restart: <i>Live</i> and a <i>Reconnected</i> toast.", False),
    (32, "On the phone", "Open the dashboard on your iPhone.",
     "Order is power, then mode controls, then status. Buttons are easy to tap and nothing scrolls sideways.", False),
    (33, "Headline explains why", "Look at the headline in Eco, Max solar and Custom.",
     "The headline says what is happening in plain words (<i>Charging at 10 A</i> with <i>Using 2.40 kW: 2.03 kW from the sun</i> under it, or the reason it is not charging). Switch on a big load (oven) in Eco while the sun is out and it says <i>Home is using the solar</i>; under cloud or at dusk it says <i>Not enough solar yet</i>. The pill at the top is green <i>Live</i>; unplug the Envoy in the daytime and after a few minutes it turns amber and reads <i>Meter 5m old</i>.", False),
    (34, "Custom still shows live meters", "Choose Custom while the car is charging, by day or by night.",
     "Solar, Home and Grid show real values and <i>Updated ... ago</i>. The log shows <font face='Courier'>[FAST]</font> lines. "
     "Nothing is sent to the charger because of them, and unplugging the Envoy shows an error line but never a fail-safe.", False),
    (38, "Fits on a phone", "Open the dashboard on your phone, in portrait.",
     "Without scrolling you see the headline with Pause, the four power tiles, today's three numbers and the mode buttons. Nothing scrolls sideways. Turn the phone: the graph redraws at the new width.", True),
    (39, "Today's graph and activity fill in", "Leave it running for an hour or two while the car charges, then touch the graph.",
     "Solar, car, home and grid import appear, the charging strip shows when the car charged, and touching the graph reads out the values for that time. The Activity list explains each start, stop and change in words.", False),
    (40, "Savings add up", "Charge from the sun for a while, then set the electricity price (gear icon, Settings) to 40 and then 20.",
     "<i>Saved today</i> grows with the sun's share of the car's energy and rescales with the price. <i>From the sun</i> is close to what you expect on a sunny day. Restart the app: today's graph and totals are still there.", False),
    (41, "History tabs", "After a few days of charging, open the graph card and press 7 days, 30 days and Year.",
     "Each shows stacked bars (the car's energy from the sun in yellow, from the grid in red), with a line saying how much went to the car, the share from the sun and what you saved. The totals match what you expect from the days you charged.", False),
    (42, "Tap a day", "In the 7 days or 30 days view, tap one day's bar, then the chip at the top right.",
     "The card shows that day's curve (solar, car, home, grid import) under the day's name, then the chip returns you to the bars. Days older than 30 days still show a curve, at lower detail.", False),
    (43, "History survives a restart", "Restart the app (or press RESET on the board, or cut its power for a minute). Open Today and 30 days.",
     "Today's curve, the activity list and the daily bars are still there. After a power cut or a hard kill (<font face='Courier'>sudo systemctl kill -s SIGKILL solar-charger</font> on the Pi) at most the last minute is missing: both save a small journal every minute and a full copy every 30 minutes.", True),
    (44, "Export", "Open /api/export.csv (for example http://wattsync.local/api/export.csv) in a browser.",
     "A spreadsheet file downloads with one line per day: date, solar, car, car from the sun, grid import and grid export in kWh.", False),
    (52, "Pull down to refresh", "Open the page from its Home Screen icon, then pull the page down from the very top and let go.",
     "The WattSync logo slides down in a small round badge, growing as you pull while its sync arrows turn; the bolt pops and glows once you have pulled far enough. Letting go reloads the page and the arrows spin around the bolt while it loads. A short pull springs back without reloading. No text appears. It does nothing in a normal Safari tab (Safari has its own) and does not start from a slider.", False),
    (53, "Other sites cannot change settings", "Run <font face='Courier'>curl -X POST 'http://wattsyncpi.local:8080/api/pause?enabled=true'</font> (no header), then the same with <font face='Courier'>-H 'X-WattSync: 1'</font>, then resume.",
     "Without the header the answer is 403 and nothing changes. With it, charging pauses. The dashboard buttons always work.", False),
    (54, "Departure goal waits, then takes over", "In Settings turn on <i>Ready by</i> (the switch), set the energy to 10 kWh and the time about an hour from now (say 3 pm), with a car plugged in, in Eco, outside or inside solar hours.",
     "Early on nothing changes. Once the time left is only just enough, the headline says <i>Charging to make your goal</i> with kWh so far, the speed and <i>grid power if needed</i>, and the Activity list notes it. After the deadline the goal moves to tomorrow. Turn it off with the switch. It is ignored in Custom, while paused and with no car.", False),
    (55, "Maximum current caps everything", "Set <i>Maximum current</i> to 16 in Settings, then try Eco with lots of sun and Custom at 20 A (the slider stops at 16).",
     "No mode ever asks the charger for more than 16 A, and Custom is lowered if it was higher. The new value is kept after a restart.", False),
    (56, "Peak price", "Turn on <i>Higher price at certain hours</i>, set the price to 50 cents and the hours 16 to 21 and charge from the sun in and out of those hours.",
     "<i>Saved today</i> counts the sun at the price of the hour it was used, and its small line says <i>30¢, peak 50¢</i>. Turning the switch off goes back to one price.", False),
    (57, "Messages to your phone", "Create a webhook address (Slack, Discord, Home Assistant or similar), paste it into <i>Messages to</i> in Settings, save and press <i>Send test</i>. Then end a charge and unplug the Envoy for 3 minutes.",
     "The test message arrives. So do <i>Charging ended</i> and <i>Lost contact with the solar meter</i> (and <i>Solar meter is back</i>), the same message at most once every 10 minutes. The page only ever shows the host of the address; clearing the box and saving turns messages off.", False),
    (58, "Setup check", "Open Settings, then <i>Diagnostics</i>.",
     "A short checklist says whether the charger is connected, the solar meter is readable and the clock is set, each with what to do when it is not.", False),
    (59, "Damaged settings are safe", "Stop the app, damage the state file (and delete the .bak copy next to it), start it again.",
     "The app starts <b>paused</b> and the Activity list says the saved settings were damaged. Nothing charges until you press Resume.", False),
    (60, "Board starts without internet", "Unplug the router's internet cable (keep Wi-Fi) and restart the board.",
     "The dashboard loads within seconds. Charging control starts as soon as the clock is set (from the router if the internet is missing), and <i>Diagnostics</i> shows the clock state.", False),
    (51, "Grid allowance slider waits", "Drag the Grid allowance slider and let go.",
     "Nothing is sent while you move it: the line under it counts down <i>Changing to 500 W in 4 s…</i> and one change goes out once the slider has been still for 5 seconds, then a toast confirms it.", False),
    (50, "Diagnostics survive a crash", "Open the gear, then <i>Diagnostics</i>. Press RESET on the board (a normal restart) and open it again.",
     "<i>Running for</i> starts over, <i>Last restart</i> says <i>reset pin / RESET button</i> and <i>Restarts counted</i> goes up by one with no new crash. After a real crash (or watchdog, or brownout) <i>Last crash</i> keeps what happened and when, even after later normal restarts.", True),
    (49, "Update the board over Wi-Fi", "With firmware that includes OTA already on the board, run <font face='Courier'>pio run -e esp32s3-ota -t upload</font> from the Mac, ideally while the car is not charging.",
     "The monitor shows <font face='Courier'>OTA: new firmware coming in</font>, progress lines and a restart; the board is back on the dashboard within about 30 seconds, with its settings and history intact. A wrong password in <font face='Courier'>secrets.h</font> gives <font face='Courier'>OTA: update failed: wrong password</font> and the old firmware keeps running.", True),
    (48, "Help buttons explain things", "Tap the small blue <i>i</i> next to Grid allowance, the mode buttons, Saved today and Solar hours.",
     "A short card opens in plain words. <i>Got it</i>, a tap outside it, or Esc closes it. Nothing changes on the page and nothing scrolls sideways on a phone.", False),
    (47, "Adjust the solar hours", "Tap the gear icon at the top right. Change <i>Start</i> to 15 and <i>Stop</i> to 90, then press the check mark.",
     "A toast says <i>Solar hours updated</i>, the window text shows the new times (sunrise + 15 min to sunset - 90 min) and the Activity list notes the change. Restart the app or the board: the values stay. Values outside 0 to 180 are refused.", False),
    (45, "Charging sessions list", "Charge for a while, then unplug the car or stop the charge. Look at the Charging sessions tile.",
     "While charging it shows <i>Charging now since ...</i> with kWh so far. After the stop it becomes a finished entry with kWh, the share from the sun and why it ended. Restart the app: the list is still there.", False),
    (46, "No gaps on a wide screen", "Open the dashboard on a laptop (wider than 900 px).",
     "Two columns of tiles with no empty space between Settings and Activity or anywhere else. On a phone it is one column in the order headline, power flow, numbers, modes, graph, activity, sessions, settings.", False),
    (35, "Pause stops charging in any mode", "While the car charges, press <i>Pause charging</i>. Try it in Eco, Max solar and Custom.",
     "Within a second <font face='Courier'>RemoteStopTransaction</font> is sent and charging stops. A banner says <i>Charging is paused</i>, the button "
     "now reads <i>Resume charging (Eco)</i>, and the mode itself is unchanged.", True),
    (36, "Nothing restarts while paused", "With the pause on, switch modes, move the sliders, unplug and plug the car back in, and start a charge from the wallbox's own app.",
     "No start or limit command is ever sent, and a charge started from the wallbox app is stopped again. Changes are kept (the toast says <i>Still paused</i>).", False),
    (37, "Pause survives a restart", "With the pause on and the car plugged in, restart the app (<font face='Courier'>sudo systemctl restart solar-charger</font>, or the ESP32's RESET). Then press <i>Resume charging</i>.",
     "After the restart the dashboard still shows the pause banner and nothing charges. After Resume, the earlier mode takes over again (Eco starts on real surplus, Custom starts at its set current).", True)])

S += [Spacer(1, 10), Paragraph("Useful log filters", h2),
      code_block('journalctl -u solar-charger --since "1 hour ago" --no-pager \\\n    | grep -E "DISPATCH|status:|answered|FAILSAFE|Restored|DAYLIGHT"')]

def footer(c, d):
    c.saveState(); c.setFont("Helvetica", 8); c.setFillColor(colors.HexColor("#888888"))
    c.drawString(0.7 * inch, 0.45 * inch, "WattSync - test scenarios")
    c.drawRightString(letter[0] - 0.7 * inch, 0.45 * inch, "Page %d" % d.page); c.restoreState()

doc = SimpleDocTemplate(OUT, pagesize=letter, leftMargin=0.7 * inch, rightMargin=0.7 * inch,
                        topMargin=0.6 * inch, bottomMargin=0.7 * inch, title="Test Scenarios for WattSync",
                        author="WattSync")
doc.build(S, onFirstPage=footer, onLaterPages=footer)
print("wrote", OUT)
