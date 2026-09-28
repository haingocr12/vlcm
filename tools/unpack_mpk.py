#!/usr/bin/env python3
"""Giải nén gói MPK của Mobot (Mobot.mpk, và gói MPK nhúng trong ember.dll).

Định dạng (dịch ngược từ AutoVLCMO.exe, hàm ghi gói tại 0x6363e3):
  header 28 byte: "MPK\\0", u16 ver=2, u16 0, u32 12, u32 entry_size=40,
                  u32 count, u32 table_off, u32 data_off
  entry 40 byte:  u64 name_hash, md5[16] (MD5 của dữ liệu gốc),
                  u32 flags, u32 raw_size, u32 stored_size, u32 offset (tính từ data_off)
  flags 0x100: dữ liệu bị XOR lặp với md5[16]  (khóa nằm ngay trong bảng!)
  flags 0x080: nén zlib
Tên file không được lưu (chỉ có hash 64-bit), nên file ra được đặt theo hash.

Cách dùng: python3 unpack_mpk.py <file.mpk|ember.dll> <thư_mục_ra>
"""
import hashlib
import os
import struct
import sys
import zlib


def unpack(buf, base, outdir):
    _, ver, _, _, esz, cnt, toff, doff = struct.unpack_from("<4sHHIIIII", buf, base)
    os.makedirs(outdir, exist_ok=True)
    for i in range(cnt):
        e = base + toff + i * esz
        nh, = struct.unpack_from("<Q", buf, e)
        md5 = buf[e + 8:e + 24]
        flags, raw, stored, off = struct.unpack_from("<IIII", buf, e + 24)
        data = bytearray(buf[base + doff + off:base + doff + off + stored])
        if flags & 0x100:
            for j in range(len(data)):
                data[j] ^= md5[j % 16]
        if flags & 0x80:
            data = zlib.decompress(bytes(data))
        ok = hashlib.md5(data).digest() == md5 and len(data) == raw
        name = f"{i:03d}_{nh:016x}.bin"
        open(os.path.join(outdir, name), "wb").write(data)
        print(f"{name} flags={flags:#x} raw={raw} md5={'OK' if ok else 'SAI'}")


if __name__ == "__main__":
    src, out = sys.argv[1], sys.argv[2]
    buf = open(src, "rb").read()
    pos = buf.find(b"MPK\x00\x02\x00")
    while pos >= 0:
        print(f"--- gói MPK tại offset {pos:#x}")
        unpack(buf, pos, os.path.join(out, f"mpk_{pos:x}"))
        pos = buf.find(b"MPK\x00\x02\x00", pos + 4)
