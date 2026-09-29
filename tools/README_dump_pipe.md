# Công cụ tự động hoá: dump MctHost + bắt named pipe

Các script làm theo `HUONGDAN_dump_va_bat_pipe.md`, thay cho các bước bấm tay.
Chạy trên **máy Windows cô lập (có snapshot)**, bằng **cmd/PowerShell Administrator**.

Cài đặt một lần:
```
pip install frida frida-tools pefile
```
Tải `pe-sieve32.exe` và `hollows_hunter32.exe` (hasherezade) về một thư mục, ví dụ `C:\tools`.

| Phần | Script | Thay cho |
|---|---|---|
| A1–A3 | `dump/dump_mct.ps1` → gọi `dump/check_dump.py` | tìm PID, chờ, chạy pe-sieve, xem entropy/strings |
| B1–B4 | `pipe/pipe_sniffer.py` + `pipe/hook_pipe.js` (Frida) | x32dbg + breakpoint CreateFileW/ReadFile/WriteFile + lưu gói tay |
| B (dự phòng) | `pipe/x32dbg_pipe.txt` | gõ lệnh breakpoint tay trong x32dbg |
| D | `analyze/packet_diff.py` | so sánh byte bằng mắt |

---

## Phần A — Dump MctHost

```powershell
cd tools\dump
powershell -ExecutionPolicy Bypass -File dump_mct.ps1 -ToolsDir C:\tools
# chọn đúng acc:           -Instance client_1      hoặc  -TargetPid 4321
# so entropy với bản gốc:  -Original "D:\auto\MctHost.exe"
# dump mọi MctHost:        -HollowsHunter
```
Script sẽ:
1. Tìm `MctHost.exe` là con của `AutoVLCMO.exe` và in danh sách PID + dòng lệnh.
2. Chờ đến khi tiến trình đã chạy đủ `-WaitSec` giây (mặc định 150).
3. Chạy `pe-sieve32 /pid <PID> /dmode 3 /shellc /data 3 /dir dump_mct`.
4. Chạy `check_dump.py`: in entropy từng section (≈6 là đã unpack, >7.2 bị đánh dấu), số hàm import,
   trích chuỗi ASCII + UTF-16 ra `*.strings.txt` và in các chuỗi khớp từ khoá (pipe, item, trade…).

Chạy riêng: `python check_dump.py dump_mct\process_4321 --original MctHost.exe -k TenTuKhoa`.

Nếu import = 0 hoặc hỏng thì làm bước A4 bằng Scylla trong x32dbg. Bước này vẫn làm tay.

## Phần B — Bắt named pipe

```
cd tools\pipe
python pipe_sniffer.py -n AutoVLCMO.exe -o cap
# bắt cả hai đầu:  python pipe_sniffer.py -n AutoVLCMO.exe -n MctHost.exe -o cap
```
- Tên pipe (B2) được in ra dạng `OPEN CreateFileW \\.\pipe\...` và lưu vào `cap\pipes.txt`.
- Nếu attach **sau** khi pipe đã mở, script vẫn bắt được: nó nhận diện handle kiểu pipe bằng
  `GetFileType` và lấy tên bằng `GetFileInformationByHandleEx`. Dùng `--only-named` để tắt cơ chế này
  (khi đó chỉ bắt pipe mở sau lúc attach).
- Mỗi gói (B3) được lưu thành `cap\00012_<nhãn>_<tiến trình>_<W|R>_<số byte>.bin`. ReadFile được lấy
  **sau khi hàm trả về**, kể cả đọc overlapped (qua `GetOverlappedResult`).
- **Gắn nhãn hành động (B4):** trong lúc chạy, gõ `nhat_1_mon` rồi Enter, sau đó làm thao tác trong game.
  Các gói tiếp theo sẽ mang nhãn đó. Nhật ký nằm ở `cap\log.csv`
  (`time,label,process,pid,dir,handle,pipe,len,file`). Gõ `q` để thoát.

Không dùng Frida được thì dùng x32dbg: attach → Script → Load `x32dbg_pipe.txt` → Run. Log nằm ở tab Log.

## Phần D — Tìm struct / opcode

```
cd tools\analyze
python packet_diff.py group ..\pipe\cap                       # gom gói theo nhãn/độ dài/byte đầu -> đoán opcode
python packet_diff.py diff  cap\00010_sao3.bin cap\00020_sao2.bin   # byte nào đổi
python packet_diff.py locate a_sao3.bin=3 b_sao2.bin=2 --size 1 --record 64   # offset trường "sao"
python packet_diff.py locate giu1000.bin=1000 giu2000.bin=2000              # offset số đồng (u16/u32)
python packet_diff.py find  goi.bin 1000 2000                 # tìm giá trị bất kỳ (u8/u16/u32 LE)
python packet_diff.py records tui.bin --size 64 --start 0x10  # xem mảng record cố định + cột thay đổi
```
`locate` là kỹ thuật "diff có kiểm soát" ở mục D: mỗi file đi kèm giá trị bạn biết chắc, script trả về
các offset mà **mọi** file đều chứa đúng giá trị của nó. Càng nhiều cặp thì kết quả càng ít nhiễu.

## Lưu ý
- Cả hai tiến trình đều 32-bit. Frida chạy từ Python 64-bit vẫn inject được.
- Anti-debug/VMProtect trong MctHost có thể phát hiện Frida. Nên hook **AutoVLCMO** trước, vì ở đó ít bảo vệ hơn.
- Attach có thể làm game treo hoặc rớt. Dùng acc phụ và rollback snapshot nếu cần.
