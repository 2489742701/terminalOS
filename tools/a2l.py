"""Decode ESP32-S3 panic backtrace addresses to source lines.

Usage:
  python tools/a2l.py 0x420e32ff 0x420204f8 ...
Reads addresses from argv, or from a file whose lines contain 0x... tokens.
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ELF = os.path.join(ROOT, ".wb_build", "esp32s3", "firmware.elf")
A2L = r"C:\Users\longyaosi\.platformio\packages\toolchain-xtensa-esp32s3\bin\xtensa-esp32s3-elf-addr2line.exe"


def collect(args):
    addrs = []
    for a in args:
        if os.path.isfile(a):
            with open(a, "r", errors="ignore") as f:
                txt = f.read()
            addrs += re.findall(r"0x4[0-9a-fA-F]{6,7}", txt)
        else:
            addrs += re.findall(r"0x4[0-9a-fA-F]{6,7}", a)
    seen = set()
    out = []
    for a in addrs:
        if a not in seen:
            seen.add(a)
            out.append(a)
    return out


def main():
    if not os.path.isfile(ELF):
        print("ELF not found:", ELF)
        return 1
    addrs = collect(sys.argv[1:])
    if not addrs:
        print("no addresses given")
        return 1
    print("ELF:", ELF, os.path.getsize(ELF))
    print("-" * 78)
    for a in addrs:
        r = subprocess.run([A2L, "-e", ELF, "-f", "-C", "-i", a],
                           capture_output=True, text=True, errors="ignore")
        print("%-12s %s" % (a, (r.stdout or "").strip().replace("\n", " | ")))
    return 0


if __name__ == "__main__":
    sys.exit(main())
