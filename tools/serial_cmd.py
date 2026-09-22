"""
serial_cmd.py - 向 ESP32 发送串口命令并读取响应

用法：
  python serial_cmd.py COM7 "help"
  python serial_cmd.py COM7 "nav browser"
  python serial_cmd.py COM7 "browser http://info.cern.ch/"
  python serial_cmd.py COM7 "mem"

第二个参数是要发送的命令，发送后会读取 3 秒响应输出。
"""
import serial, time, sys

port = sys.argv[1] if len(sys.argv) > 1 else 'COM7'
cmd = sys.argv[2] if len(sys.argv) > 2 else 'help'
read_timeout = float(sys.argv[3]) if len(sys.argv) > 3 else 3.0

s = serial.Serial(port, 115200, timeout=0.5)
time.sleep(0.1)
s.reset_input_buffer()

# 发送命令（加换行符触发执行）
s.write((cmd + '\n').encode('utf-8'))
s.flush()
print(f">>> {cmd}", flush=True)

# 读取响应
end = time.time() + read_timeout
while time.time() < end:
    line = s.readline()
    if line:
        try:
            print(line.decode('utf-8', 'replace').rstrip(), flush=True)
        except:
            print(repr(line), flush=True)

s.close()