import os, time, glob

ROOT = r'C:\Users\longyaosi\Downloads\C3713 4.0inch_ESP32-4848S040'
BASE = os.path.join(ROOT, 'geek-terminal', '.wb_build', 'esp32s3')


def ts(p):
    return time.strftime('%m-%d %H:%M:%S', time.localtime(os.path.getmtime(p)))


names = ['icons.cpp', 'lv_draw_sw_arc.c', 'layout_engine.cpp', 'lvgl_renderer.cpp', 'dom_renderer.cpp']
for n in names:
    hits = []
    for p in glob.glob(os.path.join(ROOT, '**', n), recursive=True):
        if '.wb_build' in p or '.pio' in p:
            continue
        hits.append(p)
    for h in hits:
        print('SRC  %-22s %s' % (n, ts(h)))

print('-' * 60)
for n in ['icons.cpp.o', 'lv_draw_sw_arc.c.o', 'layout_engine.cpp.o', 'lvgl_renderer.cpp.o', 'dom_renderer.cpp.o']:
    for p in glob.glob(os.path.join(BASE, '**', n), recursive=True):
        print('OBJ  %-24s %s' % (n, ts(p)))

print('-' * 60)
for n in ['firmware.elf', 'firmware.bin']:
    p = os.path.join(BASE, n)
    if os.path.exists(p):
        print('OUT  %-24s %s  %d bytes' % (n, ts(p), os.path.getsize(p)))
