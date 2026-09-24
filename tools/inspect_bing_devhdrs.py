"""用设备同款请求头（Connection: close / 无 Accept-Encoding）复现，看必应给什么。"""
import ssl, sys, re
try:
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
except Exception:
    pass

ctx = ssl.create_default_context()
ctx.check_hostname = False
ctx.verify_mode = ssl.CERT_NONE

UA_NEW = ('Mozilla/5.0 (Linux; Android 13; Pixel 7) AppleWebKit/537.36 '
          '(KHTML, like Gecko) Chrome/120.0.0.0 Mobile Safari/537.36')
UA_OLD = ('Mozilla/5.0 (Linux; Android 4.4.2; Nexus 5 Build/KOT49H) '
          'AppleWebKit/537.36 (KHTML, like Gecko) Chrome/30.0.0.0 Mobile Safari/537.36')


def raw_get(ua, path='/search?q=esp32'):
    import socket
    s = socket.create_connection(('cn.bing.com', 443), timeout=20)
    ss = ctx.wrap_socket(s, server_hostname='cn.bing.com')
    req = ('GET %s HTTP/1.1\r\n'
           'Host: cn.bing.com\r\n'
           'User-Agent: %s\r\n'
           'Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n'
           'Accept-Language: zh-CN,zh;q=0.9\r\n'
           'Connection: close\r\n\r\n' % (path, ua)).encode('utf-8')
    ss.sendall(req)
    buf = b''
    while True:
        try:
            c = ss.recv(65536)
        except Exception:
            break
        if not c:
            break
        buf += c
    try:
        ss.close()
    except Exception:
        pass
    return buf


def summarize(tag, buf):
    head, _, body = buf.partition(b'\r\n\r\n')
    txt = body.decode('utf-8', 'replace')
    print('%-22s bytes=%-7d b_algo=%-3d b_pag=%-3d 下一页=%-3d 上一页=%-3d' %
          (tag, len(body), txt.count('b_algo'), txt.count('b_pag'),
           txt.count('下一页'), txt.count('上一页')))
    for m in list(re.finditer(r'<a[^>]+href="([^"]*)"[^>]*>\s*下一页', txt))[:3]:
        print('     下一页 href:', m.group(1)[:110])
    for m in list(re.finditer(r'<a[^>]+href="([^"]*)"[^>]*>\s*2\s*</a>', txt))[:3]:
        print('     页码2 href:', m.group(1)[:110])


for tag, ua in [('KitKat(旧)', UA_OLD), ('Android13(新)', UA_NEW)]:
    try:
        summarize(tag, raw_get(ua))
    except Exception as e:
        print(tag, 'ERR', e)

# 翻页验证（新 UA）
try:
    b = raw_get(UA_NEW, '/search?q=esp32&first=11')
    t = b.partition(b'\r\n\r\n')[2].decode('utf-8', 'replace')
    ts = [re.sub(r'<[^>]+>', '', m.group(1)).strip()[:38]
          for m in re.finditer(r'<h2[^>]*>(.*?)</h2>', t, re.S)]
    print('\nAndroid13 first=11 前2条:')
    for x in ts[:2]:
        print('   ', x)
except Exception as e:
    print('p2 ERR', e)
