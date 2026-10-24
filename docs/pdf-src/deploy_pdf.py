from reportlab.lib.pagesizes import letter
from reportlab.lib import colors
from reportlab.lib.styles import getSampleStyleSheet, ParagraphStyle
from reportlab.lib.units import inch
from reportlab.platypus import (SimpleDocTemplate, Paragraph, Spacer, Preformatted, Table, TableStyle,
                                KeepTogether)

import os
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "deploy-to-raspberry-pi.pdf")

ss = getSampleStyleSheet()
body = ParagraphStyle("body", parent=ss["Normal"], fontSize=10.5, leading=15, spaceAfter=6)
h1 = ParagraphStyle("h1", parent=ss["Title"], fontSize=22, leading=26, textColor=colors.HexColor("#0b3d91"), spaceAfter=4)
sub = ParagraphStyle("sub", parent=body, textColor=colors.HexColor("#555555"), spaceAfter=14)
h2 = ParagraphStyle("h2", parent=ss["Heading2"], fontSize=14, leading=18, textColor=colors.HexColor("#0b3d91"),
                    spaceBefore=14, spaceAfter=6, keepWithNext=1)
step = ParagraphStyle("step", parent=ss["Heading3"], fontSize=11.5, leading=15, spaceBefore=10, spaceAfter=3)
code = ParagraphStyle("code", fontName="Courier", fontSize=9, leading=12, leftIndent=8, rightIndent=8)
lead = ParagraphStyle("lead", parent=body, keepWithNext=1)
small = ParagraphStyle("small", parent=body, fontSize=9.5, textColor=colors.HexColor("#444444"))

def code_block(text):
    t = Table([[Preformatted(text, code)]], colWidths=[6.6 * inch])
    t.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, -1), colors.HexColor("#f3f5f8")),
        ("BOX", (0, 0), (-1, -1), 0.5, colors.HexColor("#cfd6df")),
        ("TOPPADDING", (0, 0), (-1, -1), 6), ("BOTTOMPADDING", (0, 0), (-1, -1), 6),
    ]))
    return t

def note(text):
    t = Table([[Paragraph(text, small)]], colWidths=[6.6 * inch])
    t.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, -1), colors.HexColor("#fff7e0")),
        ("BOX", (0, 0), (-1, -1), 0.5, colors.HexColor("#e5c365")),
        ("TOPPADDING", (0, 0), (-1, -1), 6), ("BOTTOMPADDING", (0, 0), (-1, -1), 6),
    ]))
    return t

def settings_table(rows):
    data = [[Paragraph(x, ParagraphStyle("th", parent=body, fontName="Helvetica-Bold", fontSize=9, leading=12,
                                         textColor=colors.white, spaceAfter=0)) for x in ("Variable", "Default", "Meaning")]]
    cell = ParagraphStyle("td", parent=body, fontSize=9, leading=12, spaceAfter=0)
    for name, default, meaning in rows:
        data.append([Paragraph("<font face='Courier'>%s</font>" % name, cell), Paragraph(default, cell), Paragraph(meaning, cell)])
    t = Table(data, colWidths=[1.6 * inch, 1.85 * inch, 3.15 * inch], repeatRows=1)
    t.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, 0), colors.HexColor("#0b3d91")),
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
        ("GRID", (0, 0), (-1, -1), 0.4, colors.HexColor("#cfd6df")),
        ("ROWBACKGROUNDS", (0, 1), (-1, -1), [colors.white, colors.HexColor("#f8fafc")]),
        ("TOPPADDING", (0, 0), (-1, -1), 4), ("BOTTOMPADDING", (0, 0), (-1, -1), 5),
    ]))
    return t

def block(title, desc, cmd=None):
    items = [Paragraph(title, step)]
    if desc:
        items.append(Paragraph(desc, body))
    if cmd:
        items.append(code_block(cmd))
        items.append(Spacer(1, 4))
    return KeepTogether(items)

story = [
    Paragraph("Run WattSync on the Raspberry Pi", h1),
    Paragraph("Set up the Enphase token, keep the app running in the background, and read its logs. "
              "Target: <b>wattsync@wattsyncpi.local</b>", sub),

    Paragraph("Why a service", h2),
    Paragraph("Started by hand over SSH, the app stops when the connection drops. A systemd service keeps it "
              "running after you disconnect, starts it on boot, restarts it if it crashes, and stores its logs.", body),

    Paragraph("Setup", h2),
    block("1. Check Java on the Pi",
          "Java 17 or newer is required. Note the path printed by <font face='Courier'>which java</font>; "
          "it is used in step 5.",
          "java -version && which java"),
    block("2. Copy the jar from your Mac",
          "Run this on the Mac. Skip it if the jar is already on the Pi.",
          "scp /Users/dsanem/solar/SmartEnergyApp/target/solar-charger-1.0-SNAPSHOT.jar \\\n"
          "    wattsync@wattsyncpi.local:~/solar-charger.jar"),
    block("3. Store the Enphase token in a protected file",
          "Use an editor rather than echo, so the token does not end up in your shell history.",
          "sudo nano /etc/solar-charger.env"),
    Paragraph("Put these lines in the file, with your real token and no quotes. Save with Ctrl+O, Enter, then Ctrl+X. "
              "The voltage line is optional: it defaults to 230, so set 240 for a US supply or delete the line.", lead),
    code_block("ENPHASE_TOKEN=paste-your-token-here\nHOUSEHOLD_VOLTAGE=240"),
    block("4. Make the file readable only by root", None,
          "sudo chmod 600 /etc/solar-charger.env"),
    block("5. Create the service",
          "Change the <font face='Courier'>java</font> path if step 1 showed a different one.",
          "sudo nano /etc/systemd/system/solar-charger.service"),
    Paragraph("Paste this into the file:", lead),
    code_block("[Unit]\n"
               "Description=Solar charger controller\n"
               "After=network-online.target\n"
               "Wants=network-online.target\n"
               "\n"
               "[Service]\n"
               "User=wattsync\n"
               "WorkingDirectory=/home/wattsync\n"
               "EnvironmentFile=/etc/solar-charger.env\n"
               "ExecStart=/usr/bin/java -jar /home/wattsync/solar-charger.jar\n"
               "Restart=always\n"
               "RestartSec=10\n"
               "\n"
               "[Install]\n"
               "WantedBy=multi-user.target"),
    block("6. Enable it and start it now", None,
          "sudo systemctl daemon-reload && sudo systemctl enable --now solar-charger"),
    block("7. Check that it is running", None,
          "systemctl status solar-charger"),
    Paragraph("The dashboard is then at <font face='Courier'>http://wattsyncpi.local:8080</font>. "
              "The charger connects to <font face='Courier'>ws://wattsyncpi.local:8080/ocpp/wallbox/&lt;chargerId&gt;</font>.", body),

    Paragraph("Reading the logs", h2),
    block("Follow live", "Ctrl+C stops following. It does not stop the app.",
          "journalctl -u solar-charger -f"),
    block("Last 200 lines", None,
          "journalctl -u solar-charger -n 200 --no-pager"),
    block("Since this morning", None,
          "journalctl -u solar-charger --since today"),
    Paragraph("systemd rotates the logs, so they will not fill the disk.", body),

    Paragraph("Day-to-day", h2),
    block("Deploy a new build",
          "Copy the jar with the scp command from step 2, then restart the service.",
          "sudo systemctl restart solar-charger"),
    block("Stop the app", None,
          "sudo systemctl stop solar-charger"),
    block("Change the token or voltage",
          "Edit the env file, then restart the service.",
          "sudo nano /etc/solar-charger.env\nsudo systemctl restart solar-charger"),

    Paragraph("Settings", h2),
    Paragraph("Set these in <font face='Courier'>/etc/solar-charger.env</font> (one <font face='Courier'>NAME=value</font> per line, "
              "no quotes), then restart the service. Only the token is required.", body),
    settings_table([
        ("ENPHASE_TOKEN", "required", "Enphase access token. The app will not start without it."),
        ("HOUSEHOLD_VOLTAGE", "230", "Volts used to turn watts into amps. Use 240 on a US supply."),
        ("SITE_LATITUDE", "37.8412", "Latitude for sunrise and sunset (default is Lathrop, CA)."),
        ("SITE_LONGITUDE", "-121.2691", "Longitude, negative for the western hemisphere."),
        ("SITE_TIMEZONE", "America/Los_Angeles", "Timezone used to show the daylight window."),
        ("STATE_FILE", "solar-charger-state.json", "Where the app saves its state, relative to its working directory."),
    ]),
    Spacer(1, 6),
    Paragraph("These are changed on the dashboard and kept across restarts: the mode, the Custom current, the grid allowance "
              "(0 to 1000 W) and the daylight window on/off. Timers, limits and smoothing are constants in "
              "<font face='Courier'>AppConfig.java</font> and need a rebuild.", body),

    Paragraph("Daylight window", h2),
    Paragraph("ECO and Max solar only run from 30 minutes after sunrise to 1 hour before sunset (both adjustable under the gear icon, Settings), calculated from the date and the "
              "location above with no internet needed. Outside the window the app stops polling the Envoy, stops asking the charger "
              "for meter values, and stops a running charge once. Custom mode is not affected. Today's window is in the startup log "
              "(<font face='Courier'>Daylight window today: ...</font>) and on the dashboard, where a checkbox turns the window off.", body),

    Paragraph("Saved state", h2),
    Paragraph("The app saves its mode, the Custom current, the grid allowance, the daylight setting, and what it last knew about the charger (status, transaction, "
              "current limit) to <font face='Courier'>/home/wattsync/solar-charger-state.json</font>. After a crash or restart it "
              "reads this file, shows the charger as <i>restored, verifying</i> on the dashboard, and asks the charger for its real "
              "status. Saved charger state older than one hour is ignored.", body),
    block("Reset to defaults",
          "Stop the app, delete the file, and start it again. To keep the file somewhere else, add a "
          "<font face='Courier'>STATE_FILE=/path/to/file.json</font> line to the env file.",
          "sudo systemctl stop solar-charger\nrm /home/wattsync/solar-charger-state.json\nsudo systemctl start solar-charger"),

    Paragraph("Troubleshooting", h2),
    note("<b>Do not also run it by hand.</b> Starting <font face='Courier'>java -jar ...</font> in a terminal while the "
         "service is running fails, because port 8080 is already in use."),
    Spacer(1, 6),
    Paragraph("<b>&quot;Environment variable ENPHASE_TOKEN is not set&quot;</b> in the log: the env file path or its line "
              "format is wrong. Check that it is <font face='Courier'>/etc/solar-charger.env</font> and each line is "
              "<font face='Courier'>NAME=value</font>.", body),
    Paragraph("<b>&quot;Enphase rejected the access token&quot;</b> in the log or on the dashboard: the token has expired. "
              "Put a new one in the env file and restart the service.", body),
    Paragraph("<b>Service will not start:</b> run <font face='Courier'>journalctl -u solar-charger -n 50 --no-pager</font> "
              "for the reason. A wrong Java path in the service file is the usual cause.", body),
    Paragraph("<b>Old token:</b> it was hardcoded in the original source file, so regenerate it in your Enphase account "
              "if you can.", body),
]

# Keep each section heading on the same page as its first block.
merged, i = [], 0
while i < len(story):
    item = story[i]
    if isinstance(item, Paragraph) and item.style is h2 and i + 1 < len(story) and isinstance(story[i + 1], KeepTogether):
        merged.append(KeepTogether([item] + list(story[i + 1]._content)))
        i += 2
    else:
        merged.append(item)
        i += 1
story = merged

def footer(canvas, doc):
    canvas.saveState()
    canvas.setFont("Helvetica", 8)
    canvas.setFillColor(colors.HexColor("#888888"))
    canvas.drawString(0.9 * inch, 0.5 * inch, "WattSync - Raspberry Pi deployment")
    canvas.drawRightString(letter[0] - 0.9 * inch, 0.5 * inch, "Page %d" % doc.page)
    canvas.restoreState()

doc = SimpleDocTemplate(OUT, pagesize=letter, leftMargin=0.9 * inch, rightMargin=0.9 * inch,
                        topMargin=0.6 * inch, bottomMargin=0.75 * inch,
                        title="Run WattSync on the Raspberry Pi", author="WattSync")
doc.build(story, onFirstPage=footer, onLaterPages=footer)
print("wrote", OUT)
