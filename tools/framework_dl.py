
import urllib.request, hashlib, os
proxy={"http":"http://127.0.0.1:10808","https":"http://127.0.0.1:10808"}
op=urllib.request.build_opener(urllib.request.ProxyHandler(proxy))
url="https://dl.registry.ns3.platformio.org/tools/7d/27/65c8307ca9af8e7f653da8375459ebf0566a70a79e8dc3fe8134897ba296/framework-arduinoespressif32-3.20017.241212+sha.dcc1105b.tar.gz"
sha="65c8307ca9af8e7f653da8375459ebf0566a70a79e8dc3fe8134897ba296"
dst=r"C:/Users/longyaosi/.platformio/.cache/downloads/"+sha
for attempt in range(12):
    start=os.path.getsize(dst) if os.path.exists(dst) else 0
    try:
        req=urllib.request.Request(url)
        if start: req.add_header("Range","bytes=%d-"%start)
        with op.open(req, timeout=120) as r, open(dst,"ab" if start else "wb") as f:
            while True:
                c=r.read(1024*1024)
                if not c: break
                f.write(c)
        h=hashlib.sha256()
        with open(dst,"rb") as f:
            for c in iter(lambda:f.read(1024*1024),b""): h.update(c)
        if h.hexdigest()==sha:
            print("VERIFIED framework size", os.path.getsize(dst)); break
        else:
            print("sha mismatch, retry"); os.remove(dst)
    except Exception as e:
        print("attempt",attempt,"err",repr(e)[:120],"size",os.path.getsize(dst) if os.path.exists(dst) else 0)
print("DONE exists",os.path.exists(dst),"size",os.path.getsize(dst) if os.path.exists(dst) else 0)
