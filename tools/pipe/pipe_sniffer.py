#!/usr/bin/env python3
"""Bắt named pipe giữa AutoVLCMO.exe <-> MctHost.exe (Phần B của hướng dẫn).

Thay cho việc đặt breakpoint tay trong x32dbg: script inject Frida vào tiến trình,
hook CreateFileW / WaitNamedPipeW / ReadFile / WriteFile ... rồi:
  * in ra TÊN pipe (B2),
  * lưu mỗi gói Read/Write thành một file .bin (B3),
  * ghi nhật ký `thời điểm | hành động | Read/Write | số byte | file` vào log.csv (B4).

Trong lúc chạy, gõ một nhãn rồi Enter (vd: "ban 1 mon") -> các gói tiếp theo sẽ
được gắn nhãn đó cho tới khi đổi nhãn khác. Gõ "q" để thoát.

Cài đặt (Windows, cmd Administrator):
    pip install frida frida-tools
Ví dụ:
    python pipe_sniffer.py -n AutoVLCMO.exe
    python pipe_sniffer.py -n AutoVLCMO.exe -n MctHost.exe -o cap_ban_do
    python pipe_sniffer.py -p 1234 --only-named
Python 64-bit vẫn inject được vào tiến trình 32-bit.
"""
import argparse
import csv
import os
import sys
import threading
import time

import frida

HERE = os.path.dirname(os.path.abspath(__file__))


def hexdump(data: bytes, width: int = 16, limit: int = 128) -> str:
    lines = []
    for off in range(0, min(len(data), limit), width):
        chunk = data[off:off + width]
        hx = " ".join(f"{b:02X}" for b in chunk)
        asc = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
        lines.append(f"    {off:04X}  {hx:<{width * 3}} {asc}")
    if len(data) > limit:
        lines.append(f"    ... ({len(data) - limit} byte nữa)")
    return "\n".join(lines)


class Capture:
    def __init__(self, out_dir: str, quiet: bool):
        self.out_dir = out_dir
        self.quiet = quiet
        os.makedirs(out_dir, exist_ok=True)
        self.label = "chua_gan_nhan"
        self.seq = 0
        self.lock = threading.Lock()
        new = not os.path.exists(os.path.join(out_dir, "log.csv"))
        self.csv_f = open(os.path.join(out_dir, "log.csv"), "a", newline="", encoding="utf-8")
        self.csv = csv.writer(self.csv_f)
        if new:
            self.csv.writerow(["time", "label", "process", "pid", "dir", "handle", "pipe", "len", "file"])
        self.pipes_f = open(os.path.join(out_dir, "pipes.txt"), "a", encoding="utf-8")

    def set_label(self, label: str):
        with self.lock:
            self.label = label.strip().replace(" ", "_") or "chua_gan_nhan"
        print(f"[*] Nhãn hiện tại: {self.label}")

    def on_message(self, proc: str, pid: int, msg, data):
        if msg["type"] == "error":
            print(f"[!] {proc}:{pid} lỗi script: {msg.get('stack') or msg}")
            return
        p = msg["payload"]
        t = p.get("type")
        ts = time.strftime("%H:%M:%S")
        if t == "log":
            print(f"[{ts}] {proc}:{pid} {p['msg']}")
        elif t in ("open", "close"):
            line = f"[{ts}] {proc}:{pid} {t.upper()} {p.get('api', '')} {p['pipe']} handle={p['handle']}"
            if t == "open" and not p.get("ok", True):
                line += " (THẤT BẠI)"
            print(line)
            with self.lock:
                self.pipes_f.write(line + "\n")
                self.pipes_f.flush()
        elif t == "packet":
            with self.lock:
                self.seq += 1
                name = f"{self.seq:05d}_{self.label}_{proc.split('.')[0]}_{p['dir']}_{p['len']}.bin"
                with open(os.path.join(self.out_dir, name), "wb") as f:
                    f.write(data)
                self.csv.writerow([ts, self.label, proc, pid, "Write" if p["dir"] == "W" else "Read",
                                   p["handle"], p["pipe"], p["len"], name])
                self.csv_f.flush()
            arrow = "-->" if p["dir"] == "W" else "<--"
            print(f"[{ts}] {proc}:{pid} {arrow} {p['dir']} {p['len']}B {p['pipe']} -> {name}")
            if not self.quiet:
                print(hexdump(data))


def find_pids(device, names, pids):
    found = [(pid, None) for pid in pids]
    procs = device.enumerate_processes()
    for n in names:
        hits = [(pr.pid, pr.name) for pr in procs if pr.name.lower() == n.lower()]
        if not hits:
            print(f"[!] Không thấy tiến trình {n}")
        found += hits
    names_by_pid = {pr.pid: pr.name for pr in procs}
    return [(pid, name or names_by_pid.get(pid, str(pid))) for pid, name in found]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-n", "--name", action="append", default=[], help="tên tiến trình (lặp được)")
    ap.add_argument("-p", "--pid", action="append", type=int, default=[], help="PID (lặp được)")
    ap.add_argument("-o", "--out", default="pipe_capture", help="thư mục lưu gói (mặc định pipe_capture)")
    ap.add_argument("--only-named", action="store_true",
                    help="chỉ bắt handle mở bằng CreateFileW/CreateNamedPipeW sau khi hook "
                         "(mặc định: bắt mọi handle kiểu pipe, kể cả đã mở trước khi attach)")
    ap.add_argument("-q", "--quiet", action="store_true", help="không in hexdump")
    args = ap.parse_args()
    if not args.name and not args.pid:
        args.name = ["AutoVLCMO.exe"]

    with open(os.path.join(HERE, "hook_pipe.js"), encoding="utf-8") as f:
        source = f"var ALL_PIPES_FLAG = {'false' if args.only_named else 'true'};\n" + f.read()

    cap = Capture(args.out, args.quiet)
    device = frida.get_local_device()
    targets = find_pids(device, args.name, args.pid)
    if not targets:
        sys.exit("Không có tiến trình nào để attach.")

    sessions = []
    for pid, pname in targets:
        try:
            s = device.attach(pid)
        except frida.PermissionDeniedError:
            print(f"[!] Không đủ quyền attach {pname}:{pid} - chạy cmd bằng Administrator")
            continue
        sc = s.create_script(source)
        sc.on("message", lambda m, d, pn=pname, pi=pid: cap.on_message(pn, pi, m, d))
        s.on("detached", lambda reason, *_, pn=pname, pi=pid: print(f"[!] Tách khỏi {pn}:{pi}: {reason}"))
        sc.load()
        sessions.append(s)
        print(f"[+] Đã attach {pname}:{pid}")
    if not sessions:
        sys.exit(1)

    print(f"[*] Lưu vào {os.path.abspath(args.out)}. Gõ nhãn hành động + Enter, 'q' để thoát.")
    try:
        for line in sys.stdin:
            if line.strip().lower() in ("q", "quit", "exit"):
                break
            cap.set_label(line)
    except KeyboardInterrupt:
        pass
    for s in sessions:
        try:
            s.detach()
        except Exception:
            pass
    print(f"[*] Xong. {cap.seq} gói đã lưu.")


if __name__ == "__main__":
    main()
