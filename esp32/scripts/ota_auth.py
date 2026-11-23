# PlatformIO post-build step for the esp32s3-ota environment: hand the OTA password from src/secrets.h to the uploader,
# so it is never typed on the command line or kept in platformio.ini.
import os
import re

Import("env")  # noqa: F821  (provided by PlatformIO)

secrets = os.path.join(env["PROJECT_DIR"], "src", "secrets.h")  # noqa: F821
password = None
if os.path.exists(secrets):
    match = re.search(r'#define\s+OTA_PASSWORD\s+"([^"]*)"', open(secrets).read())
    if match:
        password = match.group(1)
if not password:
    raise SystemExit("OTA_PASSWORD is missing from src/secrets.h (see src/secrets.example.h)")
# espota prints every option, password included, when --debug is on; drop it (the progress bar stays).
flags = [f for f in env["UPLOADERFLAGS"] if f != "--debug"]  # noqa: F821
env.Replace(UPLOADERFLAGS=flags + ["--auth=" + password])  # noqa: F821
