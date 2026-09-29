#!/usr/bin/env python3
"""Kiểm tra bản dump của pe-sieve (Phần A3 của hướng dẫn).

Với mỗi file .exe/.dll/.shc trong thư mục dump:
  * in entropy từng section (PE) — ~6 là đã unpack, ~7.9 là còn nén/mã hoá;
  * so với file gốc nếu có --original;
  * trích chuỗi ASCII + UTF-16LE ra <file>.strings.txt (tương đương `strings -a` + `strings -el`)
    và in các chuỗi khớp từ khoá quan tâm (pipe, item, trade...).

Cách dùng:
    python check_dump.py dump_mct\\process_1234 [--original MctHost.exe] [--min 5] [-k tu_khoa ...]
Cần: pip install pefile
"""
import argparse
import math
import os
import re
import sys
from collections import Counter

try:
    import pefile
except ImportError:
    pefile = None

KEYWORDS = ["pipe", "item", "bag", "trade", "sell", "shop", "npc", "star", "level", "quality",
            "lock", "gold", "money", "xu", "vang", "ban", "huy", "giao dich", "packet", "send", "recv"]


def entropy(data: bytes) -> float:
    if not data:
        return 0.0
    n = len(data)
    return -sum(c / n * math.log2(c / n) for c in Counter(data).values())


def sections(path):
    if pefile is None:
        return None
    try:
        pe = pefile.PE(path, fast_load=True)
    except pefile.PEFormatError:
        return None
    out = []
    for s in pe.sections:
        name = s.Name.rstrip(b"\0").decode(errors="replace")
        out.append((name, s.VirtualAddress, s.Misc_VirtualSize, s.SizeOfRawData, entropy(s.get_data())))
    pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_IMPORT"]])
    n_imp = sum(len(e.imports) for e in getattr(pe, "DIRECTORY_ENTRY_IMPORT", []))
    return out, n_imp


def extract_strings(data: bytes, min_len: int):
    ascii_re = re.compile(rb"[\x20-\x7e]{%d,}" % min_len)
    utf16_re = re.compile(rb"(?:[\x20-\x7e]\x00){%d,}" % min_len)
    for m in ascii_re.finditer(data):
        yield m.start(), "A", m.group().decode("ascii")
    for m in utf16_re.finditer(data):
        yield m.start(), "U", m.group().decode("utf-16le")


def report_pe(path, label):
    info = sections(path)
    if info is None:
        print(f"  (không phải PE hợp lệ hoặc thiếu pefile) entropy toàn file = {entropy(open(path, 'rb').read()):.2f}")
        return {}
    secs, n_imp = info
    print(f"  {label}: {len(secs)} section, {n_imp} hàm import")
    print(f"    {'Section':<10}{'VA':>10}{'VSize':>10}{'Raw':>10}  Entropy")
    res = {}
    for name, va, vs, raw, e in secs:
        flag = "  <-- còn nén/mã hoá?" if e > 7.2 else ""
        print(f"    {name:<10}{va:>10X}{vs:>10X}{raw:>10X}  {e:5.2f}{flag}")
        res[name] = e
    return res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dump_dir")
    ap.add_argument("--original", help="file MctHost.exe gốc (đã pack) để so sánh")
    ap.add_argument("--min", type=int, default=5, help="độ dài chuỗi tối thiểu")
    ap.add_argument("-k", "--keyword", action="append", help="thêm từ khoá tìm trong chuỗi")
    args = ap.parse_args()
    kws = [k.lower() for k in KEYWORDS + (args.keyword or [])]
    if pefile is None:
        print("[!] Chưa cài pefile (pip install pefile) - chỉ tính entropy toàn file.")

    orig = {}
    if args.original:
        print(f"== Gốc: {args.original}")
        orig = report_pe(args.original, "file gốc")

    files = sorted(f for f in os.listdir(args.dump_dir)
                   if f.lower().endswith((".exe", ".dll", ".shc", ".bin")))
    if not files:
        sys.exit(f"Không có file dump nào trong {args.dump_dir}")
    for f in files:
        path = os.path.join(args.dump_dir, f)
        data = open(path, "rb").read()
        print(f"\n== {f} ({len(data)} byte)")
        if not f.lower().endswith(".shc"):
            cur = report_pe(path, "dump")
            for name, e in cur.items():
                if name in orig:
                    print(f"    {name}: {orig[name]:.2f} -> {e:.2f}")
        else:
            print(f"  shellcode, entropy = {entropy(data):.2f}")

        strs = list(extract_strings(data, args.min))
        out = path + ".strings.txt"
        with open(out, "w", encoding="utf-8") as fo:
            for off, kind, s in sorted(strs):
                fo.write(f"{off:08X} {kind} {s}\n")
        hits = [(o, k, s) for o, k, s in strs if any(kw in s.lower() for kw in kws)]
        print(f"  {len(strs)} chuỗi -> {os.path.basename(out)}; {len(hits)} chuỗi khớp từ khoá")
        for o, k, s in sorted(hits)[:40]:
            print(f"    {o:08X} {k} {s[:100]}")
        if len(hits) > 40:
            print(f"    ... xem thêm trong {os.path.basename(out)}")


if __name__ == "__main__":
    main()
