"""
serial_cmd.py - 向 ESP32 发送串口命令并读取响应

用法：
  python serial_cmd.py COM7 "help"
  python serial_cmd.py COM7 "nav browser"
  python serial_cmd.py COM7 "browser http://info.cern.ch/"
  python serial_cmd.py COM7 "mem"
  python serial_cmd.py COM7 "nav weather|weather" 20   # 多命令，| 分隔
  python serial_cmd.py COM7 "|mem" 15                  # 开头空 = 先等设备就绪

第二个参数是要发送的命令（可用 | 分隔多条），第三个是每条命令后等待的秒数
（默认 3 秒）。

═══ 三个铁律（都是踩过才知道的） ═══
1. 打开串口后**必须** setDTR(False) + setRTS(False)。
   否则 CH340 把 EN 拉住，设备一直在复位态，一个字节都不吐 ——
   现象是"串口完全没反应"，很容易误判成线坏了 / 波特率不对。

2. **不要用 readline()**：read 超时会返回半行，拼起来看着像设备输出乱码。
   必须整段累积，最后再 splitlines。

3. **命令发早了会被丢**：设备刚上电时命令发过去直接丢掉，要等
   "[Console] Serial Console ready" 出现之后再发。用空命令（cmd 以 | 开头，
   或整串就是空）可以先等待。

pyserial 只在系统 python-sdk 3.13.2 里装了，managed python 没有：
  C:/Users/longyaosi/python-sdk/python3.13.2/python.exe tools/serial_cmd.py COM7 "help"
"""
import sys
import time
import serial

READY_MARK = b'[Console] Serial Console ready'


def main():
    port = sys.argv[1] if len(sys.argv) > 1 else 'COM7'
    cmds = (sys.argv[2] if len(sys.argv) > 2 else 'help').split('|')
    secs = float(sys.argv[3]) if len(sys.argv) > 3 else 3.0

    # 铁律 1：不拉 DTR/RTS，否则 CH340 把 EN 拉住，设备一直卡在复位态，
    #         一个字节都不吐（现象是"串口完全没反应"）。
    # ⚠️ 而且必须在 open() **之前**把 dtr/rts 设成 False：
    #    pyserial 打开端口的瞬间会按默认状态（True）驱动 DTR/RTS，
    #    CH340 借此拉一下 EN —— 表现为每次跑脚本设备都重启一遍
    #    （日志里又刷一次 "=== Geek Terminal Boot ==="）。
    #    想故意抓启动日志请用 tools/cap_log.py。
    s = serial.Serial()
    s.port = port
    s.baudrate = 115200
    s.timeout = 0.3
    s.dtr = False
    s.rts = False
    s.open()
    s.setDTR(False)
    s.setRTS(False)
    time.sleep(0.2)
    s.reset_input_buffer()

    def drain(seconds, stop_on_ready=False):
        """铁律 2：整段累积，不按行读。"""
        end = time.time() + seconds
        buf = b''
        while time.time() < end:
            chunk = s.read(4096)
            if chunk:
                buf += chunk
                if stop_on_ready and READY_MARK in buf:
                    break
            else:
                time.sleep(0.05)
        return buf.decode('utf-8', 'replace')

    def emit(txt):
        for ln in txt.splitlines():
            print('   ' + ln.rstrip(), flush=True)

    for c in cmds:
        if c == '':
            # 空命令 = 只等待。用于让设备完成启动 / 等 WiFi 连上。
            print('>>> (wait for ready, %.0fs max)' % secs, flush=True)
            emit(drain(secs, stop_on_ready=True))
            print('---', flush=True)
            continue
        s.write((c + '\n').encode('utf-8'))
        s.flush()
        print('>>> ' + c, flush=True)
        emit(drain(secs))
        print('---', flush=True)

    s.close()


if __name__ == '__main__':
    main()
