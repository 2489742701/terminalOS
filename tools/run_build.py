import subprocess, sys, os

PROJ = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal"
LOG = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\tools\pio_run2.log"

cmd = [sys.executable, "-m", "platformio", "run"]
print("CWD:", PROJ, flush=True)
print("CMD:", " ".join(cmd), flush=True)
# Stream directly to file to avoid pipe-buffer deadlock on long builds.
with open(LOG, "w", encoding="utf-8") as f:
    r = subprocess.run(cmd, stdout=f, stderr=subprocess.STDOUT, cwd=PROJ)
with open(LOG, "a", encoding="utf-8") as f:
    f.write("\n\n=== DONE RC=%d ===\n" % r.returncode)
print("DONE RC=%d" % r.returncode, flush=True)
