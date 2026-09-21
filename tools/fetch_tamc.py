import urllib.request, zipfile, os, shutil

PROXY = "http://127.0.0.1:10808"
BASE = r"C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040"
PROJ = os.path.join(BASE, "geek-terminal")
TMP = os.path.join(PROJ, "tools", "tamc_tmp.zip")
EXT = os.path.join(PROJ, "tools", "tamc_extract")
DEST = os.path.join(BASE, "4.0inch_ESP32-4848S040", "1-Demo", "Demo_Arduino",
                    "Libraries", "TAMC_GT911")
LOG = os.path.join(PROJ, "tools", "tamc_fetch.log")

lines = []
def logf(s):
    lines.append(str(s))
    print(str(s))

def download(url):
    proxy = urllib.request.ProxyHandler({"http": PROXY, "https": PROXY})
    opener = urllib.request.build_opener(proxy)
    req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
    with opener.open(req, timeout=180) as r:
        data = r.read()
    return data

try:
    data = None
    for branch in ("master", "main"):
        url = f"https://github.com/tamctec/TAMC_GT911/archive/refs/heads/{branch}.zip"
        try:
            logf(f"Trying {branch}: {url}")
            data = download(url)
            logf(f"  got {len(data)} bytes (magic={data[:2]})")
            if data[:2] == b"PK":
                break
            logf(f"  not a zip (branch {branch} maybe missing), try next")
            data = None
        except Exception as e:
            logf(f"  branch {branch} failed: {e}")
    if data is None or data[:2] != b"PK":
        raise RuntimeError("could not download TAMC_GT911 zip")

    with open(TMP, "wb") as f:
        f.write(data)
    logf("Saved zip to %s" % TMP)

    if os.path.isdir(EXT):
        shutil.rmtree(EXT)
    with zipfile.ZipFile(TMP) as z:
        z.extractall(EXT)
    logf("Extracted to %s" % EXT)

    entries = sorted(os.listdir(EXT))
    logf("Top entries: %s" % entries)
    inner = None
    for e in entries:
        if os.path.isdir(os.path.join(EXT, e)):
            inner = os.path.join(EXT, e)
            break
    if inner is None:
        raise RuntimeError("no directory inside zip")

    if os.path.isdir(DEST):
        shutil.rmtree(DEST)
    os.makedirs(DEST, exist_ok=True)
    for item in os.listdir(inner):
        s = os.path.join(inner, item)
        d = os.path.join(DEST, item)
        if os.path.isdir(s):
            shutil.copytree(s, d)
        else:
            shutil.copy2(s, d)
    logf("Copied into %s" % DEST)

    h = os.path.join(DEST, "TAMC_GT911.h")
    lp = os.path.join(DEST, "library.properties")
    src = os.path.join(DEST, "src")
    logf("TAMC_GT911.h exists: %s" % os.path.isfile(h))
    logf("library.properties exists: %s" % os.path.isfile(lp))
    logf("src/ dir exists: %s" % os.path.isdir(src))
    # If header is at root but no src/, ensure PIO can find it:
    # PIO adds both lib root and lib/src to includes, so root header is fine.
    logf("DEST contents: %s" % sorted(os.listdir(DEST)))
except Exception as ex:
    logf("ERROR: %s" % ex)

with open(LOG, "w", encoding="utf-8") as f:
    f.write("\n".join(lines))
