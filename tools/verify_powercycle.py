import serial, time, sys, subprocess

PORT = "COM7"
BAUD = 115200
LOG = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040\geek-terminal\tools\zoom_pc.log"

# 真正断电重启：禁/启 CH340 PnP 设备会切断 ESP32 的 USB 供电
ps = r'''
$id = (Get-PnpDevice -Class Ports | Where-Object { $_.FriendlyName -match "CH340" }).InstanceId
if ($id) {
    Disable-PnpDevice -InstanceId $id -Confirm:$false
    Start-Sleep -Seconds 4
    Enable-PnpDevice -InstanceId $id -Confirm:$false
    Start-Sleep -Seconds 3
    Write-Output ("power cycled: " + $id)
} else { Write-Output "no CH340" }
'''
r = subprocess.run(["powershell", "-NoProfile", "-Command", ps],
                   check=False, capture_output=True, text=True, timeout=30)
print("pnp:", r.stdout.strip(), r.stderr.strip())

ser = serial.Serial(PORT, BAUD, timeout=1)
ser.setDTR(False)
ser.setRTS(False)
time.sleep(1.0)
print("capturing 30s ...")
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
