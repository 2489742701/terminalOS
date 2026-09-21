import sys, os, hashlib, time, tarfile

PROXY = {"http": "http://127.0.0.1:10808", "https": "http://127.0.0.1:10808"}


def log(m):
    print(m, flush=True)


from platformio import compat
from platformio.registry.client import RegistryClient
from platformio.registry.mirror import RegistryFileMirrorIterator

# ---- 1) lock the exact framework version the platform requires ----
rc = RegistryClient()
pkg = rc.get_package("tool", "platformio", "framework-arduinoespressif32")
target = "3.20017.241212"
ver = None
for v in pkg["versions"]:
    if target in str(v.get("name", "")):
        ver = v
        break
assert ver, "framework version %s not found" % target
f0 = ver["files"][0]  # system == '*' (universal)
dl_url = f0["download_url"]
checksum = f0["checksum"]["sha256"]  # file sha256
log("registry download_url: " + dl_url)
log("file sha256(checksum): " + checksum)

# ---- 2) resolve the real redirect Location exactly like PIO ----
it = RegistryFileMirrorIterator(dl_url)
url, _ = next(it)
log("resolved Location: " + url)

# ---- 3) compute the exact PIO cache key: sha1(url + checksum) ----
h = hashlib.new("sha1")
h.update(compat.hashlib_encode_data(url))
h.update(compat.hashlib_encode_data(checksum))
key = h.hexdigest()
cache_dir = os.path.join(os.path.expanduser("~"), ".platformio", ".cache", "downloads")
os.makedirs(cache_dir, exist_ok=True)
dst = os.path.join(cache_dir, key)
tmp = dst + ".fwseed.tmp"
log("PIO cache key: " + key)
log("dst: " + dst)

import urllib.request
op = urllib.request.build_opener(urllib.request.ProxyHandler(PROXY))


def get_head():
    req = urllib.request.Request(url, method="HEAD")
    with op.open(req, timeout=30) as r:
        return int(r.headers.get("Content-Length", 0)), r.headers.get("Accept-Ranges")


total, ar = get_head()
log("HEAD total=%d accept-ranges=%s" % (total, ar))
range_ok = bool(ar) and "bytes" in ar.lower()


def sha_of(path):
    hh = hashlib.sha256()
    with open(path, "rb") as fp:
        for chunk in iter(lambda: fp.read(1 << 20), b""):
            hh.update(chunk)
    return hh.hexdigest()


# ---- 4) range-resume download to temp ----
has = os.path.getsize(tmp) if os.path.exists(tmp) else 0
errors = 0
while True:
    if os.path.exists(tmp):
        has = os.path.getsize(tmp)
    if total > 0 and has >= total:
        break
    req = urllib.request.Request(url)
    if range_ok and has > 0:
        req.add_header("Range", "bytes=%d-" % has)
    try:
        with op.open(req, timeout=120) as r:
            mode = "ab" if (has > 0 and os.path.exists(tmp)) else "wb"
            with open(tmp, mode) as fp:
                while True:
                    buf = r.read(1 << 20)
                    if not buf:
                        break
                    fp.write(buf)
                    fp.flush()
                    has += len(buf)
            got = sha_of(tmp)
            if got == checksum:
                log("sha256 OK size=%d" % has)
                break
            log("sha mismatch got=%s expected=%s -> restart" % (got[:16], checksum[:16]))
            os.remove(tmp)
            has = 0
    except Exception as e:
        errors += 1
        log("err#%d at %d: %s ; retry in 3s" % (errors, has, e))
        time.sleep(3)

got = sha_of(tmp)
if got != checksum:
    log("FATAL sha mismatch, not installing: %s" % got)
    sys.exit(2)
if os.path.exists(dst):
    os.remove(dst)
os.rename(tmp, dst)
log("PLACED cache file: " + dst)

# ---- 5) bonus: confirm LCD_CAM_LCD_UPDATE exists in framework headers ----
try:
    with tarfile.open(dst, "r:gz") as tf:
        cand = None
        for m in tf.getmembers():
            if m.name.endswith("lcd_cam_reg.h") and "esp32s3" in m.name:
                cand = m
                break
        if cand:
            buf = tf.extractfile(cand).read().decode("utf-8", "replace")
            for macro in ("LCD_CAM_LCD_UPDATE", "LCD_CAM_LCD_CMD", "LCD_CAM_LCD_START"):
                for line in buf.splitlines():
                    if "#define" in line and macro in line:
                        log("HEADER %s -> %s" % (macro, line.strip()))
                        break
        else:
            log("lcd_cam_reg.h(esp32s3) not found in tarball")
except Exception as e:
    log("header check skipped: %s" % e)

log("DONE")
