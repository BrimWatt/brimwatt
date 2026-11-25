# Tesla Fleet API set-up tool

A small Python 3 script (standard library only; it also needs `openssl`) for the one-time steps of the Tesla account link. Run it on your computer.

```bash
python3 tools/tesla_fleet/tesla_fleet.py register                              # tell Tesla about the app's domain (once per region)
python3 tools/tesla_fleet/tesla_fleet.py setup-board --board http://brimwatt.local   # put the app on the board (once)
python3 tools/tesla_fleet/tesla_fleet.py login [--board http://brimwatt.local]        # sign in on this computer instead of on the board's page
```

It reads `~/.brimwatt/tesla-app.env` (mode 600, never inside the repository) with `TESLA_CLIENT_ID`, `TESLA_CLIENT_SECRET` and `TESLA_DOMAIN`,
and the app's private key from `~/.brimwatt/tesla-app-private.pem`. Secrets are never printed, and the refresh token is not written to disk.

On macOS, your terminal needs the **Local Network** permission to reach the board.

What the steps do, and what is still untested, is in [`docs/tesla-fleet-api.md`](../../docs/tesla-fleet-api.md).

The folder used to be called `~/.wattsync` (the project's earlier name); the tool still uses it if `~/.brimwatt` does not exist.
