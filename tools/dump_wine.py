#!/usr/bin/env python3
"""Dump vùng image (0x400000–0x800000) của một tiến trình PE32 chạy dưới Wine.

Dùng để lấy code đã được VMProtect giải nén (vd. MctHost.exe).
Chạy song song với chương trình, cần quyền đọc /proc/<pid>/mem (root):

  python3 dump_wine.py <thư_mục_ra> mcthost.exe &
  xvfb-run -a wine MctHost.exe ...

Mỗi mốc thời gian ghi dump_<pid>_<t>.bin (offset = VA - 0x400000) và .map.
"""
import os
import sys
import time

BASE, END = 0x400000, 0x800000
TIMES = [0.5, 1, 2, 3, 5, 8, 12, 20, 30, 45]


def find_pids(name):
    out = []
    for p in os.listdir("/proc"):
        if not p.isdigit():
            continue
        try:
            cmd = open(f"/proc/{p}/cmdline", "rb").read().lower()
        except OSError:
            continue
        if name.encode() in cmd and b"python" not in cmd:
            out.append(int(p))
    return out


def dump(pid, path):
    total = 0
    maps = open(f"/proc/{pid}/maps").read().splitlines()
    with open(f"/proc/{pid}/mem", "rb") as mem, open(path + ".bin", "wb") as f, \
            open(path + ".map", "w") as mf:
        for line in maps:
            lo, hi = (int(x, 16) for x in line.split()[0].split("-"))
            if not (BASE <= lo < END) or "r" not in line.split()[1]:
                continue
            try:
                mem.seek(lo)
                data = mem.read(hi - lo)
            except OSError:
                mf.write(line + " ERR\n")
                continue
            f.seek(lo - BASE)
            f.write(data)
            total += len(data)
            mf.write(line + "\n")
    return total


if __name__ == "__main__":
    out, name = sys.argv[1], sys.argv[2].lower()
    t0 = time.time()
    for t in TIMES:
        while time.time() - t0 < t:
            time.sleep(0.05)
        for pid in find_pids(name):
            try:
                n = dump(pid, f"{out}/dump_{pid}_{t}")
            except OSError as e:
                print(f"t={t} pid={pid} lỗi: {e}", flush=True)
                continue
            print(f"t={t} pid={pid} bytes={n}", flush=True)
