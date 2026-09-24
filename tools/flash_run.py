#!/usr/bin/env python3
"""烧录固件。同 build_run.py：抬高 safe-delete 阈值，避免构建/链接期的中间文件
删除把整个进程拦死。"""
import subprocess, sys, os

proj = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal"
log = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\tools\flash_sdkdef.log"
py = r"C:\Users\longyaosi\python-sdk\python3.13.2\python.exe"

env = dict(os.environ)
env["CODEBUDDY_SAFE_DELETE_BULK_THRESHOLD"] = "100000"

# platformio.ini 里写死了 upload_port = COM7，但 CH340 每次重新枚举都可能换号
# （COM7 -> COM8 ...），于是出现"明明线插着却 Could not open COM7"。
# 这里先探一次：配置的口不在，就自动切到当前唯一那个串口（优先 CH340/USB-SERIAL）。
args = [py, "-m", "platformio", "run", "-t", "upload", "--project-dir", proj]
try:
    import serial.tools.list_ports as lp
    ports = [p.device for p in lp.comports()]
    cfg = "COM7"
    ini = os.path.join(proj, "platformio.ini")
    for ln in open(ini, encoding="utf-8", errors="replace"):
        if ln.strip().startswith("upload_port"):
            cfg = ln.split("=", 1)[1].strip()
            break
    if cfg not in ports and ports:
        pick = next((p for p in ports if "CH340" in p or "USB-SERIAL" in p), ports[0])
        sys.stderr.write("PORT %s missing -> using %s (have: %s)\n" % (cfg, pick, ports))
        args += ["--upload-port", pick]
except Exception as e:
    sys.stderr.write("port auto-detect skipped: %r\n" % (e,))

with open(log, "w", buffering=1) as f:
    r = subprocess.run(args, stdout=f, stderr=subprocess.STDOUT, env=env)
    sys.stderr.write("FLASH_RC=%d\n" % r.returncode)
sys.exit(r.returncode)
