#!/usr/bin/env python3
"""Giải mã data\\game.pak của vlcmcliet.exe thành file SWF.

Định dạng (dịch ngược từ vlcmcliet.exe, hàm tại 0x40c6a0):
  [0:16]    salt
  [16:32]   IV (AES-CBC)
  [32:-32]  ciphertext (AES-256-CBC, PKCS7)
  [-32:]    HMAC-SHA256(mac_key, IV || ciphertext)
  key = PBKDF2-HMAC-SHA256(PASSWORD, salt, 100000 vòng, 64 byte)
        -> key[0:32] = khóa AES, key[32:64] = khóa HMAC

Cách dùng: python3 decrypt_pak.py game.pak game.swf
Cần: pip install pycryptodome
"""
import hashlib
import hmac
import sys

from Crypto.Cipher import AES

PASSWORD = b"xQ9#hI3u!28@91992$$Lm&zW"  # hằng số trong .rdata của vlcmcliet.exe
ITERATIONS = 100_000


def decrypt(data: bytes) -> bytes:
    salt, iv, ct, tag = data[:16], data[16:32], data[32:-32], data[-32:]
    key = hashlib.pbkdf2_hmac("sha256", PASSWORD, salt, ITERATIONS, 64)
    mac = hmac.new(key[32:], iv + ct, hashlib.sha256).digest()
    if not hmac.compare_digest(mac, tag):
        raise ValueError("HMAC sai - file hỏng hoặc khóa khác")
    pt = AES.new(key[:32], AES.MODE_CBC, iv).decrypt(ct)
    return pt[: -pt[-1]]


if __name__ == "__main__":
    src, dst = sys.argv[1], sys.argv[2]
    out = decrypt(open(src, "rb").read())
    if out[:3] not in (b"CWS", b"FWS", b"ZWS"):
        raise SystemExit("Không phải SWF")
    open(dst, "wb").write(out)
    print(f"OK: {dst} ({len(out)} bytes, SWF v{out[3]})")
