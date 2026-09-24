import serial, time, sys

PORT = "COM7"
BAUD = 115200
LOG = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\tools\zoom_clean.log"

ser = serial.Serial(PORT, BAUD, timeout=1)
# 释放复位/下载线：DTR=False, RTS=False（高电平=正常启动）
ser.setDTR(False)
ser.setRTS(False)
time.sleep(1.0)
print("capturing 30s (no command) ...")
t0 = time.time()
with open(LOG, "w", encoding="utf-8") as f:
    while time.time() - t0 < 30:
        b = ser.read(4096)
        if b:
            s = b.decode(errors="replace")
            f.write(s)
            sys.stdout.write(s)
ser.close()
print("\n=== done")
