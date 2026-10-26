#!/usr/bin/env python3
"""Prints compiler flags for OpenSSL (used only by the host tests of the Tesla code; the firmware uses mbedTLS).

PlatformIO runs this through `build_flags = !python3 scripts/native_openssl.py` in the native environment. It looks for
Homebrew's OpenSSL first (macOS), then the system's pkg-config. If it finds nothing it prints nothing and the Tesla host
tests will say OpenSSL is missing.
"""
import glob
import shutil
import subprocess

flags = ""
brew = shutil.which("brew") or "/opt/homebrew/bin/brew"
for name in ("openssl@3", "openssl"):
    try:
        prefix = subprocess.run([brew, "--prefix", name], capture_output=True, text=True).stdout.strip()
    except OSError:
        prefix = ""
    if prefix and glob.glob(prefix + "/include/openssl/evp.h"):
        flags = f"-I{prefix}/include -L{prefix}/lib -lcrypto"
        break
if not flags and shutil.which("pkg-config"):
    r = subprocess.run(["pkg-config", "--cflags", "--libs", "openssl"], capture_output=True, text=True)
    if r.returncode == 0:
        flags = r.stdout.strip()
print(flags)
