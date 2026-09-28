#!/usr/bin/env python3
"""Giải mã bảng chuỗi trong bytecode MVMP (máy ảo "mvm" của ember.dll, Mobot).

Bố cục một hàm MVMP (parser tại ember.dll 0x10647e20):
  u32 nK, nK x hằng có tag      (tag: 0 nil, 1/2 bool, 3 u32, 9 -u32, 4 số dạng chuỗi,
                                  5 chuỗi, 11 u32+u32)
  u32 nS, nS x (u32 len, bytes) -> bảng chuỗi MÃ HÓA (+0x4c)
  u32 nD, nD x (u32 len, bytes) -> bỏ qua
  u32 nC, nC x hàm con (đệ quy)
  7 hằng có tag: f20 (seed), f10, f14, f38, f30, f18, f28
  seed thật = f20 - fmod(nK*7 + f10*13 + (f14 != 0)*31, 2^20)   (0x10648950)
  u32; u32 n + n x u32 (mã lệnh); u32; u32 m + m x (u32 idx, u32 key);
  u32 n + n byte (tag thật); u32 flags; [byte 1, u32 n + n x u32]

Hàm giải mã chuỗi (ember.dll 0x106476c0), idx tính từ 1:
  x = fmod(f20*65537 + idx*8191, 2^31)
  mỗi byte i: x = LCG(x); a = floor(x/2^16) & 255
              x = LCG(x); c = floor(x/2^8)  & 255
              k = (a + 7 + 7*i + 3*c) & 255, nếu 0 thì 0xAD;  out = in ^ k
  LCG(x) = fmod(x*1103515245 + 12345, 2^31)   (tính bằng double, như bản gốc)

Cách dùng:  python3 mvmp_strings.py <file.mvmp> [...]
            (MVMP lấy từ blob AutoAI.Deflate / mvm.run_vm_compressed sau khi raw-inflate)
"""
import math
import struct
import sys

M = 2147483648.0


class Reader:
    def __init__(self, d):
        self.d, self.p = d, 0

    def u8(self):
        v = self.d[self.p]
        self.p += 1
        return v

    def u32(self):
        v = struct.unpack_from("<I", self.d, self.p)[0]
        self.p += 4
        return v

    def blob(self):
        n = self.u32()
        v = self.d[self.p:self.p + n]
        self.p += n
        return v

    def tagged(self):
        t = self.u8()
        if t == 3:
            return float(self.u32())
        if t == 9:
            return -float(self.u32())
        if t == 4:  # số dạng chuỗi, chỉ đọc tối đa 63 byte
            n = min(self.u32(), 0x3F)
            v = self.d[self.p:self.p + n]
            self.p += n
            return float(v)
        if t == 5:
            return self.blob()
        if t == 11:
            v = float(self.u32())
            self.u32()
            return v
        if t in (1, 2):
            return t == 2  # bool: 1 false, 2 true
        return None  # nil


def lcg(x):
    x = math.fmod(x * 1103515245.0 + 12345.0, M)
    return x + M if x < 0 else x


def decrypt(s, f20, idx):
    x = math.fmod(f20 * 65537.0 + idx * 8191.0, M)
    if x < 0:
        x += M
    out = bytearray()
    for i, b in enumerate(s):
        x = lcg(x)
        a = int(math.floor(x * (1 / 65536))) & 255
        x = lcg(x)
        c = int(math.floor(x * (1 / 256))) & 255
        k = (a + 7 + 7 * i + 3 * c) & 255 or 0xAD
        out.append(b ^ k)
    return bytes(out)


def parse_func(r, out, depth=0):
    nk = r.u32()
    for _ in range(nk):
        r.tagged()
    strs = [r.blob() for _ in range(r.u32())]
    for _ in range(r.u32()):
        r.blob()
    for _ in range(r.u32()):
        parse_func(r, out, depth + 1)
    hdr = [r.tagged() for _ in range(7)]
    num = [float(h) if isinstance(h, (float, bool)) else 0.0 for h in hdr]
    f20, f10, f14 = num[0], int(num[1]), num[2]
    # ember.dll 0x10648950 chỉnh seed trước khi chạy
    f20 -= math.fmod(nk * 7.0 + f10 * 13.0 + (31.0 if f14 != 0 else 0.0), 1048576.0)
    r.u32()
    for _ in range(r.u32()):
        r.u32()
    r.u32()
    for _ in range(r.u32()):
        r.u32(); r.u32()
    n = r.u32()
    r.p += n
    r.u32()
    if r.p < len(r.d) and r.u8() == 1:  # byte cờ luôn bị đọc
        for _ in range(r.u32()):
            r.u32()
    for i, s in enumerate(strs, 1):
        out.append(decrypt(s, f20, i))


def strings_of(data):
    if data[:5] != b"MVMP\x02":
        raise ValueError("không phải MVMP v2")
    r = Reader(data)
    r.p = 5
    out = []
    parse_func(r, out)
    return out


if __name__ == "__main__":
    for path in sys.argv[1:]:
        for s in strings_of(open(path, "rb").read()):
            print(s.decode("utf-8", "backslashreplace"))
