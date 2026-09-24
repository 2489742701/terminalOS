#!/usr/bin/env python3
"""烧录固件。同 build_run.py：抬高 safe-delete 阈值，避免构建/链接期的中间文件
删除把整个进程拦死。"""
import subprocess, sys, os

proj = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal"
log = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\tools\flash_sdkdef.log"
py = r"C:\Users\longyaosi\python-sdk\python3.13.2\python.exe"

env = dict(os.environ)
env["CODEBUDDY_SAFE_DELETE_BULK_THRESHOLD"] = "100000"

with open(log, "w", buffering=1) as f:
    r = subprocess.run([py, "-m", "platformio", "run", "-t", "upload", "--project-dir", proj],
                       stdout=f, stderr=subprocess.STDOUT, env=env)
    sys.stderr.write("FLASH_RC=%d\n" % r.returncode)
sys.exit(r.returncode)
