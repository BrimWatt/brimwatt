# Security policy

## Reporting a vulnerability

Please report suspected vulnerabilities **privately**, using GitHub's "Report a vulnerability" button under this repository's **Security** tab
(Security advisories), not in a public issue. Include what you found, how to reproduce it, and the version or commit. Never include real
tokens, keys or passwords in a report. This is a volunteer project: expect a reply as time allows, with no guaranteed response time.

Only the latest commit on `main` is supported.

## What this software controls

WattSync changes the charging current of a real EV charger, and optionally of a real car, on your home network. A flaw could affect
charging, so please treat that as the impact when you assess a finding.

## Security model, in short

- The dashboard and API have **no login**. They are meant for a trusted home network and should not be exposed to the internet.
  Requests that change anything need a header (`X-WattSync: 1`) as protection against cross-site requests; that is not authentication.
- Secrets on the ESP32 board (Wi-Fi password, Enphase token, update password, Tesla signing keys and sign-in) live in the board's flash,
  which is **not encrypted**. Anyone with physical access to the board can read them. They are never printed, logged or served.
- The setup network (`WattSync-Setup`) uses one fixed password on every board. Open it only at home.
- The connection to the Enphase gateway accepts its self-signed certificate. Notification messages sent over HTTPS do not check the server's
  certificate. Connections to Tesla's Fleet API do check it, against pinned root certificates.
- The Tesla account link needs a client secret on the board (Tesla only exchanges a sign-in code with it). Use a developer app of your own
  that you can reset, and remove the account from the board when you are done.

See [Known limitations](README.md#known-limitations) for the full list.
