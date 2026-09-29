#!/usr/bin/env python3
"""Phần D của hướng dẫn: tìm offset các trường struct từ gói/bản dump đã bắt.

Lệnh con:
  diff  A.bin B.bin              so 2 gói/record, in các byte khác nhau (kỹ thuật "diff có kiểm soát")
  find  FILE VALUE [...]         tìm giá trị số (u8/u16/u32 little-endian) trong file
  locate FILE=VALUE [...]        tìm offset mà MỌI file đều chứa đúng giá trị tương ứng
                                 vd: locate sao3.bin=3 sao2.bin=2  -> offset trường "sao"
                                     locate giu1000.bin=1000 giu2000.bin=2000 --size 4
  records FILE --size N [--start S]   cắt file thành mảng record cố định N byte, in dạng bảng
  group CAPTURE_DIR              gom các gói trong log.csv theo nhãn/chiều/độ dài/byte đầu (đoán opcode)

Cách dùng: python packet_diff.py <lệnh> -h
"""
import argparse
import csv
import os
import struct
import sys
from collections import defaultdict

FMT = {1: "<B", 2: "<H", 4: "<I"}


def load(p):
    with open(p, "rb") as f:
        return f.read()


def parse_int(s):
    return int(s, 0)


def cmd_diff(a):
    x, y = load(a.a), load(a.b)
    print(f"{a.a}: {len(x)} byte, {a.b}: {len(y)} byte")
    n = min(len(x), len(y))
    diffs = [i for i in range(n) if x[i] != y[i]]
    if len(x) != len(y):
        print(f"[!] Độ dài khác nhau ({len(x)} vs {len(y)}) - so {n} byte đầu")
    if not diffs:
        print("Giống hệt nhau trong phần chung.")
        return
    # gom các offset liền nhau thành cụm
    runs, start = [], diffs[0]
    for prev, cur in zip(diffs, diffs[1:] + [None]):
        if cur is None or cur != prev + 1:
            runs.append((start, prev))
            start = cur
    print(f"{len(diffs)} byte khác, {len(runs)} cụm:")
    for s, e in runs:
        ln = e - s + 1
        line = f"  +{s:04X} ({s:5d}) len {ln:<3} {x[s:e+1].hex(' ')}  ->  {y[s:e+1].hex(' ')}"
        # gợi ý giá trị số nếu cụm nằm gọn trong 1/2/4 byte (tính từ đầu cụm)
        for size in (1, 2, 4):
            if ln <= size and s + size <= n:
                vx = struct.unpack_from(FMT[size], x, s)[0]
                vy = struct.unpack_from(FMT[size], y, s)[0]
                line += f"   u{size * 8}: {vx} -> {vy}"
                break
        print(line)
        if 1 < ln <= 16:  # các trường liền nhau hay bị gộp 1 cụm -> in từng byte
            print("        từng byte: " + ", ".join(f"+{i:X}:{x[i]}->{y[i]}" for i in range(s, e + 1)))


def matches(data, value, sizes, signed=False):
    for size in sizes:
        if value >= 1 << (size * 8):
            continue
        fmt = FMT[size]
        for off in range(0, len(data) - size + 1):
            if struct.unpack_from(fmt, data, off)[0] == value:
                yield off, size


def cmd_find(a):
    data = load(a.file)
    sizes = [a.size] if a.size else [1, 2, 4]
    for v in a.values:
        v = parse_int(v)
        hits = list(matches(data, v, sizes))
        print(f"{v} (0x{v:X}): {len(hits)} chỗ")
        for off, size in hits[: a.max]:
            ctx = data[max(0, off - 4): off + size + 4].hex(" ")
            print(f"  +{off:04X} u{size * 8}   ...{ctx}...")


def cmd_locate(a):
    pairs = []
    for item in a.pairs:
        path, _, v = item.rpartition("=")
        pairs.append((path, load(path), parse_int(v)))
    sizes = [a.size] if a.size else [1, 2, 4]
    for size in sizes:
        common = None
        for path, data, v in pairs:
            offs = {off for off, _ in matches(data, v, [size])}
            common = offs if common is None else common & offs
        if common:
            print(f"u{size * 8}: offset khớp tất cả: " + ", ".join(f"+{o:04X}" for o in sorted(common)[: a.max]))
            if a.record:
                print("   (theo record %d byte: %s)" % (a.record, ", ".join(
                    f"rec {o // a.record} +{o % a.record:X}" for o in sorted(common)[: a.max])))
        else:
            print(f"u{size * 8}: không có offset chung")


def cmd_records(a):
    data = load(a.file)[a.start:]
    n = len(data) // a.size
    print(f"{n} record x {a.size} byte (dư {len(data) % a.size})")
    print("     " + " ".join(f"{i:02X}" for i in range(a.size)))
    for r in range(min(n, a.max)):
        rec = data[r * a.size:(r + 1) * a.size]
        print(f"{r:4d} " + " ".join(f"{b:02X}" for b in rec))
    # cột nào thay đổi giữa các record -> ứng viên trường dữ liệu
    if n > 1:
        varying = [i for i in range(a.size) if len({data[r * a.size + i] for r in range(n)}) > 1]
        print("Cột thay đổi giữa các record: " + ", ".join(f"+{i:X}" for i in varying))


def cmd_group(a):
    log = os.path.join(a.dir, "log.csv")
    groups = defaultdict(list)
    with open(log, encoding="utf-8") as f:
        for row in csv.DictReader(f):
            path = os.path.join(a.dir, row["file"])
            head = load(path)[: a.head].hex(" ") if os.path.exists(path) else "?"
            groups[(row["label"], row["dir"], row["len"], head)].append(row["file"])
    print(f"{'label':<20}{'dir':<7}{'len':>6}  {'byte đầu':<{a.head * 3}} số gói")
    for (label, d, ln, head), files in sorted(groups.items()):
        print(f"{label:<20}{d:<7}{ln:>6}  {head:<{a.head * 3}} {len(files)}  (vd {files[0]})")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("diff"); p.add_argument("a"); p.add_argument("b"); p.set_defaults(fn=cmd_diff)

    p = sub.add_parser("find"); p.add_argument("file"); p.add_argument("values", nargs="+")
    p.add_argument("--size", type=int, choices=[1, 2, 4]); p.add_argument("--max", type=int, default=30)
    p.set_defaults(fn=cmd_find)

    p = sub.add_parser("locate"); p.add_argument("pairs", nargs="+", metavar="FILE=VALUE")
    p.add_argument("--size", type=int, choices=[1, 2, 4]); p.add_argument("--max", type=int, default=50)
    p.add_argument("--record", type=int, help="độ dài record để quy offset về (số record, offset trong record)")
    p.set_defaults(fn=cmd_locate)

    p = sub.add_parser("records"); p.add_argument("file"); p.add_argument("--size", type=int, required=True)
    p.add_argument("--start", type=parse_int, default=0); p.add_argument("--max", type=int, default=64)
    p.set_defaults(fn=cmd_records)

    p = sub.add_parser("group"); p.add_argument("dir"); p.add_argument("--head", type=int, default=8)
    p.set_defaults(fn=cmd_group)

    a = ap.parse_args()
    a.fn(a)


if __name__ == "__main__":
    main()
