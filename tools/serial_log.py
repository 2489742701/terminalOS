import serial, time, sys
port = sys.argv[1] if len(sys.argv) > 1 else 'COM7'
duration = int(sys.argv[2]) if len(sys.argv) > 2 else 30
s = serial.Serial(port, 115200, timeout=1)
# Reset via DTR/RTS
s.dtr = False
s.rts = True
time.sleep(0.1)
s.dtr = True
s.rts = False
time.sleep(0.1)
s.rts = True
time.sleep(0.5)
s.dtr = False
s.rts = False
time.sleep(0.5)
s.reset_input_buffer()
end = time.time() + duration
while time.time() < end:
    line = s.readline()
    if line:
        try:
            print(line.decode('utf-8', 'replace').rstrip(), flush=True)
        except:
            print(repr(line), flush=True)
s.close()
