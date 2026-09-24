#!/usr/bin/env python3
import subprocess, sys
proj = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\tools\minimal_test"
log = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\tools\minimal.log"
py = r"C:\Users\longyaosi\python-sdk\python3.13.2\python.exe"
with open(log, "w", buffering=1) as f:
    r1 = subprocess.run([py, "-m", "platformio", "run", "--project-dir", proj],
                        stdout=f, stderr=subprocess.STDOUT)
    f.write("\n=== BUILD RC=%d ===\n" % r1.returncode)
    r2 = subprocess.run([py, "-m", "platformio", "run", "-t", "upload", "--project-dir", proj],
                        stdout=f, stderr=subprocess.STDOUT)
    f.write("\n=== UPLOAD RC=%d ===\n" % r2.returncode)
sys.stderr.write("BUILD=%d UPLOAD=%d\n" % (r1.returncode, r2.returncode))
sys.exit(r2.returncode)
