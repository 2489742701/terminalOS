import serial, time, sys

PORT = "COM7"
BAUD = 115200

# 打开串口并触发一次复位（ESP32-S3 自动复位电路：拉 RTS 再释放可触发 EN）
ser = serial.Serial(PORT, BAUD, timeout=1)
time.sleep(0.1)
ser.setDTR(False)
ser.setRTS(True)    # 拉低 EN（多数 devkit 上 RTS 经反相驱动 EN）
time.sleep(0.1)
ser.setRTS(False)   # 释放 -> 复位启动
time.sleep(0.2)

t0 = time.time()
chunks = []
while time.time() - t0 < 18:
    try:
        b = ser.read(4096)
        if b:
            chunks.append(b.decode(errors="replace"))
    except Exception as e:
        chunks.append("\n[CAP_ERR] " + str(e) + "\n")
        break
ser.close()
sys.stdout.write("".join(chunks))
