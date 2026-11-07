#!/usr/bin/env python3
"""One-time Tesla Fleet API set-up for a WattSync board, run on your computer (standard library only; needs `openssl`).

  python3 tools/tesla_fleet/tesla_fleet.py register          tell Tesla about the app's domain (once per region)
  python3 tools/tesla_fleet/tesla_fleet.py login [--board http://wattsync.local] [--region na|eu] [--vin VIN]
                                                             sign in to Tesla in your browser, list the cars, and (with --board) hand the
                                                             sign-in to the board

Settings live in ~/.wattsync/tesla-app.env (never in the repository), one NAME=value per line:
  TESLA_CLIENT_ID=...        from the Tesla developer dashboard
  TESLA_CLIENT_SECRET=...    from the same page (used only here, never sent to the board)
  TESLA_DOMAIN=...           the domain that hosts the app's public key, e.g. wattsync.netlify.app
  TESLA_REDIRECT_URI=...     optional, default http://localhost:3000/oauth/tesla/ (must match the dashboard exactly)
The app's private key is ~/.wattsync/tesla-app-private.pem (made once; only its public half is hosted).

Secrets are never printed. The refresh token is NOT written to disk: Tesla's refresh tokens are single use, so the only copy that
matters is the one the board keeps. VINs are shown masked.
"""
import argparse
import http.server
import json
import os
import secrets
import subprocess
import sys
import threading
import urllib.parse
import urllib.request
import webbrowser

HOME = os.path.expanduser("~/.wattsync")
ENV_FILE = os.path.join(HOME, "tesla-app.env")
KEY_FILE = os.path.join(HOME, "tesla-app-private.pem")
AUTH_URL = "https://auth.tesla.com/oauth2/v3/authorize"
TOKEN_URL = "https://fleet-auth.prd.vn.cloud.tesla.com/oauth2/v3/token"
API = {"na": "https://fleet-api.prd.na.vn.cloud.tesla.com", "eu": "https://fleet-api.prd.eu.vn.cloud.tesla.com"}
USER_SCOPES = "openid offline_access vehicle_device_data vehicle_cmds vehicle_charging_cmds"
PARTNER_SCOPES = "openid vehicle_device_data vehicle_cmds vehicle_charging_cmds"


def die(message):
    print("Error: " + message, file=sys.stderr)
    sys.exit(1)


def load_env():
    if not os.path.exists(ENV_FILE):
        die("%s does not exist. Create it (see --help); keep it out of the repository." % ENV_FILE)
    if os.stat(ENV_FILE).st_mode & 0o077:
        die("%s must only be readable by you: run  chmod 600 %s" % (ENV_FILE, ENV_FILE))
    env = {}
    for line in open(ENV_FILE):
        line = line.strip()
        if line and not line.startswith("#") and "=" in line:
            name, value = line.split("=", 1)
            env[name.strip()] = value.strip().strip('"').strip("'")
    for name in ("TESLA_CLIENT_ID", "TESLA_CLIENT_SECRET", "TESLA_DOMAIN"):
        if not env.get(name):
            die("%s is missing from %s" % (name, ENV_FILE))
    env.setdefault("TESLA_REDIRECT_URI", "http://localhost:3000/oauth/tesla/")
    return env


def mask_vin(vin):
    return vin[:3] + "*" * (len(vin) - 7) + vin[-4:] if len(vin) == 17 else "?"


def request(method, url, data=None, headers=None, form=False):
    body = None
    headers = dict(headers or {})
    if data is not None:
        if form:
            body = urllib.parse.urlencode(data).encode()
            headers["Content-Type"] = "application/x-www-form-urlencoded"
        else:
            body = json.dumps(data).encode()
            headers["Content-Type"] = "application/json"
    req = urllib.request.Request(url, data=body, headers=headers, method=method)
    try:
        with urllib.request.urlopen(req, timeout=30) as response:
            return response.status, response.read().decode()
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode()
    except urllib.error.URLError as e:
        die("could not reach %s (%s)" % (urllib.parse.urlparse(url).netloc, e.reason))


def say_status(what, status, body):
    ok = 200 <= status < 300
    print("%s: %s (%d)" % (what, "ok" if ok else "FAILED", status))
    if not ok:
        # Tesla's error bodies carry no secrets, but cap the length anyway.
        print("  " + body[:400].replace("\n", " "))
    return ok


def private_key_hex():
    """The 32 raw bytes of the app's private key (SEC1 DER: 30 77 02 01 01 04 20 <32 bytes> ...). Returned, never printed."""
    if not os.path.exists(KEY_FILE):
        die("%s not found." % KEY_FILE)
    der = subprocess.run(["openssl", "ec", "-in", KEY_FILE, "-outform", "DER"], capture_output=True)
    if der.returncode != 0 or len(der.stdout) < 39 or der.stdout[:7] != bytes([0x30, 0x77, 0x02, 0x01, 0x01, 0x04, 0x20]):
        die("could not read the private key with openssl (expected a prime256v1 key).")
    return der.stdout[7:39].hex()


def partner_token(env, region):
    status, body = request("POST", TOKEN_URL, {
        "grant_type": "client_credentials", "client_id": env["TESLA_CLIENT_ID"], "client_secret": env["TESLA_CLIENT_SECRET"],
        "scope": PARTNER_SCOPES, "audience": API[region]}, form=True)
    if status != 200:
        say_status("Partner token", status, body)
        sys.exit(1)
    return json.loads(body)["access_token"]


def cmd_register(args):
    env = load_env()
    token = partner_token(env, args.region)
    status, body = request("POST", API[args.region] + "/api/1/partner_accounts", {"domain": env["TESLA_DOMAIN"]},
                           {"Authorization": "Bearer " + token})
    if say_status("Register %s in region %s" % (env["TESLA_DOMAIN"], args.region), status, body):
        print("Next:  python3 tools/tesla_fleet/tesla_fleet.py login --board http://wattsync.local")
    else:
        print("Check that https://%s/.well-known/appspecific/com.tesla.3p.public-key.pem opens and that the dashboard's Allowed Origin is https://%s"
              % (env["TESLA_DOMAIN"], env["TESLA_DOMAIN"]))
        sys.exit(1)


def wait_for_code(redirect_uri, state):
    parsed = urllib.parse.urlparse(redirect_uri)
    result = {}
    done = threading.Event()

    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            url = urllib.parse.urlparse(self.path)
            if url.path.rstrip("/") != parsed.path.rstrip("/"):
                self.send_response(404); self.end_headers(); return
            q = urllib.parse.parse_qs(url.query)
            if q.get("state", [""])[0] != state:
                result["error"] = "the state did not match (not started from this tool)"
            elif "code" in q:
                result["code"] = q["code"][0]
            else:
                result["error"] = q.get("error_description", q.get("error", ["no code"]))[0]
            self.send_response(200); self.send_header("Content-Type", "text/plain; charset=utf-8"); self.end_headers()
            self.wfile.write(b"WattSync: you can close this tab and go back to the terminal.")
            done.set()

        def log_message(self, *a):
            pass   # the request line holds the code: never log it

    server = http.server.HTTPServer((parsed.hostname, parsed.port or 80), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return result, done, server


def cmd_login(args):
    env = load_env()
    state = secrets.token_urlsafe(16)
    url = AUTH_URL + "?" + urllib.parse.urlencode({
        "client_id": env["TESLA_CLIENT_ID"], "redirect_uri": env["TESLA_REDIRECT_URI"], "response_type": "code",
        "scope": USER_SCOPES, "state": state, "locale": "en-US", "prompt_missing_scopes": "true"})
    result, done, server = wait_for_code(env["TESLA_REDIRECT_URI"], state)
    print("Opening Tesla's sign-in page in your browser. If it does not open, paste this address into the browser:\n  " + url)
    webbrowser.open(url)
    if not done.wait(300):
        die("no sign-in within 5 minutes.")
    server.shutdown()
    if "code" not in result:
        die("Tesla did not give a code: " + result.get("error", "unknown"))
    status, body = request("POST", TOKEN_URL, {
        "grant_type": "authorization_code", "client_id": env["TESLA_CLIENT_ID"], "client_secret": env["TESLA_CLIENT_SECRET"],
        "code": result["code"], "audience": API[args.region], "redirect_uri": env["TESLA_REDIRECT_URI"]}, form=True)
    if not say_status("Sign-in", status, body):
        sys.exit(1)
    tokens = json.loads(body)
    access, refresh = tokens["access_token"], tokens.get("refresh_token", "")
    if not refresh:
        die("Tesla gave no refresh token (is offline_access one of the app's scopes?).")

    status, body = request("GET", API[args.region] + "/api/1/vehicles", headers={"Authorization": "Bearer " + access})
    vins = []
    if say_status("List cars", status, body):
        vins = [v.get("vin", "") for v in json.loads(body).get("response", [])]
        print("  %d car(s) on this account%s" % (len(vins), ": " + ", ".join(mask_vin(v) for v in vins) if vins else "."))
    vin = (args.vin or "").replace(" ", "").upper() or (vins[0] if len(vins) == 1 else "")

    if not args.board:
        print("No --board given, so nothing was sent. The sign-in was not saved either (tokens are single use). Run again with --board.")
        return
    form = {"clientId": env["TESLA_CLIENT_ID"], "refreshToken": refresh, "region": args.region}
    if vin:
        form["vin"] = vin
    elif vins:
        die("more than one car: choose one with --vin.")
    if not args.no_key:
        form["key"] = private_key_hex()
    status, body = request("POST", args.board.rstrip("/") + "/api/car/fleet/connect", form, {"X-WattSync": "1"}, form=True)
    say_status("Send to the board", status, body if status != 200 else "")
    if status != 200:
        if "Tesla mode is off" in body:
            print("  Turn Tesla mode on in Settings > Tesla first.")
        sys.exit(1)
    print("Done. In Settings > Tesla choose Connect through: Tesla account. Then, on the phone that is your car's key, open:")
    print("  https://tesla.com/_ak/%s%s" % (env["TESLA_DOMAIN"], ("?vin=" + vin) if vin else ""))
    print("  (approve adding the app's key to the car; WattSync cannot send charge commands until that is done)")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    reg = sub.add_parser("register", help="register the app's domain with Tesla (once per region)")
    reg.add_argument("--region", choices=sorted(API), default="na")
    login = sub.add_parser("login", help="sign in to Tesla and hand the sign-in to the board")
    login.add_argument("--region", choices=sorted(API), default="na")
    login.add_argument("--board", help="the board's address, e.g. http://wattsync.local or http://192.168.1.153")
    login.add_argument("--vin", help="the car's VIN (needed when the account has several cars)")
    login.add_argument("--no-key", action="store_true", help="do not send the app's signing key (the board already has one)")
    args = parser.parse_args()
    {"register": cmd_register, "login": cmd_login}[args.command](args)


if __name__ == "__main__":
    main()
