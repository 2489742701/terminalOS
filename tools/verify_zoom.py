import serial, time, sys, subprocess

PORT = "COM7"
BAUD = 115200
LOG = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\tools\zoom_verify.log"

def reset_ch340():
    ps = r'''
    $id = (Get-PnpDevice -Class Ports | Where-Object { $_.FriendlyName -match "CH340" }).InstanceId
    if ($id) {
        Disable-PnpDevice -InstanceId $id -Confirm:$false
        Start-Sleep -Seconds 3
        Enable-PnpDevice -InstanceId $id -Confirm:$false
        Start-Sleep -Seconds 2
        Write-Output ("reset done: " + $id)
    } else { Write-Output "no CH340 found" }
    '''
    try:
        r = subprocess.run(["powershell", "-NoProfile", "-Command", ps],
                           check=False, capture_output=True, text=True, timeout=30)
        print("reset:", r.stdout.strip(), r.stderr.strip())
    except Exception as e:
        print("reset err", e)

def open_port():
    return serial.Serial(PORT, BAUD, timeout=1)

ser = None
try:
    ser = open_port()
except Exception as e:
    print("open failed, resetting CH340:", e)
    reset_ch340()
    ser = open_port()

time.sleep(0.3)
# 拉 RTS 触发一次干净复位
ser.setDTR(False)
ser.setRTS(True)
time.sleep(0.1)
ser.setRTS(False)
time.sleep(2.0)
time.sleep(3)  # 等 boot 完成

ser.write(b"browser https://www.baidu.com\n")
ser.flush()
print(">>> browser https://www.baidu.com")

t0 = time.time()
with open(LOG, "w", encoding="utf-8") as f:
    while time.time() - t0 < 55:
        b = ser.read(4096)
        if b:
            s = b.decode(errors="replace")
            f.write(s)
            sys.stdout.write(s)
ser.close()
print("\n=== capture done, see", LOG)
