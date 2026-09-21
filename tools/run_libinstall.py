import subprocess, sys, os

PROJ = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal"
LOG = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\tools\tamc_install3.log"

cmd = [sys.executable, "-m", "platformio", "lib", "install",
       "tamctec/TAMC_GT911"]
print("CWD:", PROJ)
print("CMD:", " ".join(cmd))
r = subprocess.run(cmd, capture_output=True, text=True,
                   encoding="utf-8", errors="replace", cwd=PROJ)
with open(LOG, "w", encoding="utf-8") as f:
    f.write("RC=%d\n\nSTDOUT:\n%s\n\nSTDERR:\n%s\n" % (r.returncode, r.stdout, r.stderr))
print("WROTE", LOG)
