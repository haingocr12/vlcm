# Phân tích 3 file: AutoVLCMO.exe, MctHost.exe, Datas.rar

Bộ này **khác hẳn** bộ `vlcmauto.exe / vlcmcliet.exe / game.pak` trong [REPORT.md](REPORT.md). Đây là **auto thương mại của 360AUTO** (Công ty CP Công nghệ MOBOT, Huế), bán theo gói VIP. Nó chạy game trên **giả lập Android** hoặc trên **bản PC** qua `MctHost.exe`.

| File | Kích thước | SHA-256 | Loại |
|---|---|---|---|
| `AutoVLCMO.exe` | 5 922 936 | `b3301ec0…ab401a4a` | PE32 GUI, MFC/C++ (MSVC), build 2026-09-24, v1.0.7.2 |
| `MctHost.exe` | 5 014 536 | `6b24fa7d…31350686` | PE32 GUI, **pack VMProtect**, build 2026-06-26 |
| `Datas.rar` | 10 089 | `9ce8ae66…31350686` | RAR5, 13 file XML cấu hình (2026-04-21) |

Cả hai file .exe đều có **chữ ký số EV hợp lệ** (GlobalSign GCC R45 EV CodeSigning CA 2020) cấp cho
`MOBOT TECHNOLOGY JOINT STOCK COMPANY`, MST 3301636877, 25 Đồng Khởi, P. Thủy Xuân, Huế, email `congtymobot@gmail.com`.

Thông tin phiên bản của AutoVLCMO: `CompanyName = http://360auto.vn`, `FileDescription = Auto Võ Lâm Chân Mệnh Origin`, `InternalName = Mobot`, `ProductName = Mobot Framework`.

---

## 1. Kiến trúc tổng thể

```
AutoVLCMO.exe (giao diện + bộ điều khiển, "Mobot Framework")
 ├─ Settings\Accounts.json, AppConfig.json, Statistics.db (SQLite), UserData\Users.dat
 ├─ Mobot.mpk  (gói kịch bản/logic auto, KHÔNG có trong file tải lên)
 ├─ Chế độ giả lập: Nox / MEmu / BlueStacks 5 / LDPlayer
 │     └─ điều khiển qua ADB, đẩy scrcpy-server.jar để lấy hình + gửi thao tác
 ├─ Chế độ PC:  GameHost\MctHost.exe --bootdir . -instance:<GUID>
 │     └─ host Flash (Flash.ocx, Engine.dat, register_flash_new.bat), cửa sổ "Mộng Chí Tôn"
 └─ MobotRemote: WebSocket /ws/desktop tới server của hãng (xem mục 2.5)
```

---

## 2. AutoVLCMO.exe

### 2.1 Chức năng auto (từ chuỗi giao diện tiếng Việt)

Danh sách chức năng chạy tuần tự, kéo thả để đổi thứ tự:

| Nhóm | Chức năng |
|---|---|
| Nhiệm vụ | Chính Tuyến, Phụ, Ngày, Tuần, Tuần Hoàn (hủy/đổi thưởng bằng Thiên Phạt Lệnh, "Thưởng Phạt Lệnh" để hoàn thành nhanh, dừng ở NV 10/20 hoặc 100/200) |
| Phó bản | Liên Trảm, Thiên Quan, Doanh Trại, Mê Cung (bỏ qua Mê Cung Thần Bí/Chuột), Phu Tử Trận. Có chọn độ khó, dùng phó bản lệnh, dụ quái theo "khí số liên trảm" |
| Hoạt động | Vận Tiêu cá nhân (chọn tiêu xa Đích Lô → Kỳ Lân) và Bang Hội (theo sau), Ăn Tiệc Cưới, Sóc Báu Tương Dương, BOSS, Nhập Gift Code |
| Train | Đánh quái (bãi có sẵn: Đại Thắng Quan [Cáo Đỏ], Ngưu Gia Thôn [Sói], Đào Hoa [Thanh Xà], Tuyệt Tình Cốc [Gấu Xám], Chung Nam Sơn 2 [Hươu], Tuyệt Tình Thác [Báo], Thần Điêu Cốc [Mãnh Hổ], Chung Nam Sơn [Bọ Cạp]); tự chuyển bãi, né quái biến dị/BOSS, phản kích khi bị PK |
| Ngựa | Bắt Ngựa, thăng cấp Đích Lô → Kim Ngưu → Sát Lang, thả ngựa cấp thấp khi đầy túi |
| Hậu cần | Sửa trang bị, bán/hủy trang bị theo màu/sao/cấp/môn phái, mua thuốc, cất kho, mở rộng túi |
| Nhân vật | Tăng kỹ năng bằng chân khí, tăng kinh mạch, tăng điểm tiềm năng theo tỉ lệ (T.công/P.thủ/T.pháp/S.khỏe), mặc trang bị, dùng vật phẩm/Exp x2/x5/x10 |
| Giao dịch | Người gửi/người nhận: tự đi tới điểm hẹn, gửi Đồng/TPoint/đồ cổ/mọi vật phẩm giao dịch được. Dùng để **gom đồ từ acc clone về acc chính** |
| Bày bán | Tự mở sạp bán đồ, đặt giá Đồng/Vàng |
| Tổ đội | Đội trưởng/thành viên, ghép đội giữa các giả lập, rao tin tự động, tổ đội Phu Tử/3v3 |
| Nhận quà | Quà Online, Điểm danh, Bế quan, Khai mở máy chủ, Quà vui mỗi ngày |
| Đăng nhập | Danh sách acc Tepaylink (`taikhoan\|matkhau\|maychu\|kenh\|nhanvat`), tự tạo nhân vật (tên tự đề xuất hoặc ngẫu nhiên), đổi acc khi hết việc hoặc bị khóa, khởi động lại game khi treo |
| Tiện ích | Ẩn người chơi/hiệu ứng, tắt nhạc, chống spam mời đội/kết bạn/song tu, đổi FPS |

Phím tắt khi chơi tay (chế độ "hỗ trợ"): F2 Auto PK, F3 nhảy 3 bước, F4 tự nhảy, F5 đánh người/quái, F6 combo skill, F7 tự hồi sinh, F10 buff, F11 chế độ Đao Kiếm, F12 tự lên ngựa, Shift+F1..F8 đổi kênh.

Có tab **Thống kê** lưu vào SQLite `Statistics.db` (bảng `thongke`: server, `copper`, `tpoint`, `tpoint_lock`, `tpoint_refund`…) và xuất Excel. Đây là công cụ **quản lý farm nhiều acc**.

### 2.2 Điều khiển giả lập
- Đọc cấu hình của **Nox** (`conf.ini`, `BignoxVMS`), **MEmu** (`MemuHyperv.xml`), **BlueStacks 5** (registry `SOFTWARE\BlueStacks_nxt`, `bluestacks.conf`, `ApiToken`, cổng ADB), **LDPlayer** (`dnconsole.exe`, `leidian*.vbox`).
- Sửa `bluestacks.conf`: bật `bst.enable_adb_access`, tắt quảng cáo và banner (`enable_programmatic_ads`, `enable_ai_highlights`, `launch_store_on_boot`, `enable_boot_banner`).
- Nhúng sẵn **scrcpy-server 3.3.4** (APK gốc của Genymobile, ở offset `0x46ae98`). Nó đẩy file này vào `/data/local/tmp/` để lấy luồng video H.264 và gửi thao tác. Ngoài ra có `screencap -p`.
- ⚠️ **Vá file `HD-Player.exe` của BlueStacks trên đĩa** (hàm `0x64c3eb`, gọi từ `0x63ae1a`): tìm hàm x64 có tham chiếu tới chuỗi `unlock_player.bin` trong `.text`, rồi ghi đè phần đầu hàm bằng `31 C0 C3` (`xor eax,eax; ret`) để hàm luôn trả 0. Đây là bẻ một cơ chế kiểm tra/giới hạn của BlueStacks. Việc này làm hỏng chữ ký số của `HD-Player.exe`, và bản update BlueStacks sau đó có thể lỗi.
- `NtWow64*VirtualMemory64`, `VirtualAllocEx` + `WriteProcessMemory` (hàm `0x63e8e0`): đọc PEB của một tiến trình khác, cấp vùng nhớ mới rồi **ghi đè `CommandLine`** trong `ProcessParameters`. Nghĩa là tiến trình con sẽ hiển thị dòng lệnh khác với lúc thật sự khởi chạy.
- `GetExtendedTcpTable` + `SetTcpEntry(state=DELETE_TCB)` (hàm `0x63d78d`): **cắt kết nối TCP** tới một IP:port cụ thể. Có thể dùng để ép game mất kết nối khi đổi kênh hoặc đổi acc.
- Lấy token của `explorer.exe` (`GetShellWindow` → `OpenProcessToken`), bật `SeAssignPrimaryToken/SeTcb/SeImpersonate`, tạo token hạn chế (`S-1-16-8192` = Medium IL) rồi dùng `CreateProcessWithTokenW`. Đây là kỹ thuật **chạy giả lập ở quyền thường** khi auto đang chạy bằng quyền Admin. Nó không phải leo thang quyền.

### 2.3 Chế độ PC: MctHost
- Chạy `"%s\GameHost\MctHost.exe" --bootdir . -instance:{GUID}`. Cửa sổ game có tên **"Mộng Chí Tôn"**.
- Liên quan: `Flash.ocx`, `Engine.dat`, `register_flash_new.bat`, `manifest.json` (danh sách `files` để kiểm tra hoặc cập nhật).
- Tự tìm và đóng hộp thoại lỗi `#32770` của MctHost, và kill `UnityCrashHandler64.exe`.

### 2.4 Bản quyền / VIP
- Kích hoạt bằng **mã VIP** (`MENU_VIP_ACTIVEKEY`), mua ở `vip.360auto.vn` (`/redirect/%s?redirect_uri=/keylisting`, `/shop/%d`).
- **Mã máy (HWID)** tạo ở hàm `0x645900`: code tự sửa (self-modifying) để gọi `CPUID` với nhiều leaf, trộn với chuỗi `twElsZQgwN` và hằng `0x19780102`.
- Kiểm tra **giờ hệ thống** với server. Nếu giờ máy lệch thì bắt chỉnh lại, nhằm chặn việc lùi giờ để kéo dài hạn VIP.
- Gói logic chính `Mobot.mpk` không có trong file tải lên, nên chưa phân tích được.

### 2.5 ⚠️ MobotRemote: xem và điều khiển màn hình từ xa
Có một module hoàn chỉnh `MobotRemote` (class `IScreenProvider@MobotRemote`), UA `MobotRemote/1.0` và `MobotRemote-Video/1.0`:

- Mở WebSocket tới `/ws/desktop?role=app&machine=<HWID>&product=<id>&uid=<user>&sdk=<ver>` với `Authorization: Bearer <token>`. Tên máy chủ **không lộ ra dưới dạng chuỗi tĩnh** (được truyền vào lúc chạy, nhiều khả năng lấy từ server/`Mobot.mpk`).
- Lệnh server gửi xuống: `scr_list`, `scr_thumb`, `scr_start`, `scr_stop`, `scr_take`, `scr_release`, **`scr_input`** (chuột, chạm, lăn, `keycode`), `scr_quality`, `scr_fit`, `launch`, `control`.
- Auto gửi lên: danh sách giả lập, ảnh thumbnail JPEG/WebP, luồng video (Media Foundation, `mfplat.dll`), và **toàn bộ cây giao diện của auto** (`snapshot`/`delta`, `menu_click`, `cell_edit`, `commit_text`…). Có cơ chế `redact` để che một số trường.

Đây là tính năng để người dùng **điều khiển auto qua web/điện thoại**. Nhưng hệ quả là **server của 360AUTO có thể xem màn hình giả lập/game và gửi thao tác chuột/phím** vào đó. Mình chưa xác định được lúc nào module này bật (mặc định hay phải đăng nhập/bật tay).

### 2.6 Theo dõi
- Ping thống kê tới **`whos.amung.us/pingjs/?k=<key>&t=<title>`** (hàm `0x6409a1`) với UA Firefox giả, để đếm số người đang online.
- Hỗ trợ từ xa qua **UltraViewer** (chỉ là link hướng dẫn, không nhúng sẵn).

### 2.7 Thư viện nhúng
libcurl (có SOCKS5, NTLM, LDAP), zlib/minizip, TinyXML2, OpenXLSX (xuất Excel), JSON, sqlite3.dll (import), GDI+. Proxy chỉ hỗ trợ **SOCKS5**, định dạng `host:port[:user:pass]`, và được gán cho từng giả lập (chưa hỗ trợ MEmu). Có quảng cáo WhaleProxy.

---

## 3. MctHost.exe

- **Pack bằng VMProtect 3.x**: section `.text`, `.rdata`, `.data`, `.23e` có kích thước thô bằng 0 (được giải nén lúc chạy). Code nằm trong `.kG0` (4,9 MB, entropy 7.93), entry point `0x6e3f7d` cũng ở đó. Có 3 bảng import KERNEL32. Không còn chuỗi rõ nào ngoài import và chứng chỉ, nên **không phân tích tĩnh được logic**.
- Chỉ đọc được qua import:
  - `WinHttp*` (kể cả `WinHttpCrackUrl`, `ReadData`): tải dữ liệu qua HTTP(S).
  - `SuspendThread`/`GetThreadContext`/`SetThreadContext`/`FlushInstructionCache`/`VirtualProtect` + `Thread32First/Next`: bộ **hook code trong chính tiến trình** (kiểu Detours/MinHook), nhiều khả năng dùng để hook Flash OCX.
  - `SetEnvironmentVariableW`, `SetCurrentDirectoryW`, `LoadLibraryW`: nạp Flash/`Engine.dat` theo `--bootdir`.
  - `CreateToolhelp32Snapshot`, `GetClassNameW`, `InternalGetWindowText`: liệt kê tiến trình/cửa sổ (một phần là chống debug của VMProtect).
- Manifest `asInvoker`. Chỉ có icon, không có version info.

Nói ngắn gọn, đây là **trình host game PC do Mobot tự viết** (tương tự `vlcmcliet.exe` ở bộ cũ nhưng bị bảo vệ mạnh hơn nhiều). Muốn biết chính xác nó làm gì thì phải phân tích động (chạy trong máy ảo và dump bộ nhớ sau khi VMProtect giải nén).

---

## 4. Datas.rar: dữ liệu kịch bản

| File | Nội dung |
|---|---|
| `Servers.xml` | 734 máy chủ (`gate="vlcm"`, id 1 Kim Long … 734 Hắc Lâm). **Không phải XML hợp lệ**: không có thẻ gốc, có BOM |
| `Maps_PB.xml`, `Maps_Pro.xml` | Bảng `id → tên bản đồ` + `cutline` (id 20001–20280: Tương Dương, Cổ Mộ, Chung Nam Sơn, Tuyệt Tình Cốc, Viên Nguyệt, Địa Cung, Chốn Hư Ảo, Bạch Đà Sơn Hạ…). Bản Pro có thêm Cổ Mộ Mật Thất 2–4 và các map cutline 15–16 |
| `Skills.xml` | ID kỹ năng theo nhóm: Thiếu Lâm/Toàn Chân/Cổ Mộ/Đào Hoa (3129–3168), Giang Hồ, buff (Cách Không Độ Khí 3164, Từ Hàng Phổ Độ 3147…), thú chiến (Hồi Sinh 3216…), bị động, công pháp Thần Chiếu Kinh 99201. **Lỗi dữ liệu**: Bạch Đà và Minh Giáo dùng lại y hệt ID 3133/3135/3161 của Đào Hoa (copy-paste chưa sửa) |
| `Drugs.xml` | Thuốc HP 30101–30104 (Quy/Thủy/Bích/Thần Đơn), MP 30201–30204 (Ninh/Tuyết/Vân/Bách Hoàn) |
| `Redirect.xml` | Đổi đích click: tọa độ NPC ở Tương Dương (20002) → ID NPC cửa hàng: 1638 Vũ Khí, 1639 Thuốc, 1640 Trang Sức, 1642 Gấm Vóc, 1647 Thú Cưỡi, 1650 Bí Kíp, 1090 Tiểu Nhị, 1739 Tạp Hóa. Ngoài ra còn 3 điểm đổi tọa độ quái (Loạn Quân, Thương Binh/Kỵ Binh Phản Bội) |
| `LienTram.xml`, `ThienQuan.xml`, `DoanhTrai.xml`, `DoanhTrai_5_15.xml`, `PhongAnTran.xml`, `MeCung15.xml`, `MeCungThanBi.xml` | Chuỗi tọa độ `<Point x y>` để **chạy vòng gom/quây quái** trong từng phó bản. Chú thích đầu file của Doanh Trại vẫn ghi "Liên Trảm" (copy-paste) |

Không file .exe nào ở đây tham chiếu tên các file XML này. Chúng được đọc bởi `Mobot.mpk` hoặc một bản auto khác. ID NPC 1638 trùng với `trainRepair1638` của bot SWF ở [REPORT.md](REPORT.md), tức là cả hai tool cùng chạy trên một game.

---

## 5. Đánh giá rủi ro

| Mức | Vấn đề |
|---|---|
| 🟠 Trung bình | **MobotRemote**: server của hãng có thể xem màn hình và gửi thao tác vào giả lập/game, kèm mã máy (HWID). Chưa xác định được lúc nào module này hoạt động. |
| 🟠 Trung bình | **Vá `HD-Player.exe` của BlueStacks trên đĩa** và bật ADB. Làm hỏng chữ ký số, và có thể gây lỗi khi cập nhật BlueStacks. |
| 🟠 Trung bình | **MctHost bị VMProtect**, logic chính nằm trong `Mobot.mpk` tải riêng. Người dùng không kiểm chứng được code thật sự chạy. |
| 🟡 Thấp | Ping `whos.amung.us` (theo dõi số người dùng), ghi đè command line tiến trình con, cắt kết nối TCP. Đều là tính năng phục vụ auto, nhưng là hành vi "nhạy cảm". |
| 🟢 | Có chữ ký EV của một công ty có đăng ký thật tại VN. Không thấy thêm ngoại lệ Windows Defender (khác bộ cũ), không thấy keylogger hay đào coin. Chạy giả lập bằng token Medium IL là kỹ thuật **hạ quyền**, không phải leo thang quyền. |
| ⚖️ | Vẫn là **bot game** (farm nhiều acc, gom đồ bằng giao dịch, auto PK), vi phạm điều khoản của nhà phát hành và có nguy cơ bị khóa acc. |

**So với bộ cũ (`vlcmauto`/`vlcmcliet`/`game.pak`):** bộ này chuyên nghiệp hơn (ký số, VIP, remote, hỗ trợ 4 loại giả lập) và không có mánh thêm ngoại lệ Defender hay kill-switch qua Google Drive. Bù lại, nó kiểm soát từ xa nhiều hơn (remote screen/input, HWID, kiểm tra giờ server) và mã lõi bị giấu kỹ hơn (VMProtect + `Mobot.mpk`).

**Khuyến nghị:** chỉ chạy trong máy ảo hoặc máy riêng cho game. Không đăng nhập acc quan trọng. Nếu dùng BlueStacks, sao lưu `HD-Player.exe` trước khi chạy.

---

## 6. Phương pháp
- `unrar`, `file`, `sha256sum`, `pefile` (section, entropy, import, version, tài nguyên), `openssl pkcs7` (chứng chỉ)
- Trích chuỗi ASCII và UTF-16LE (gồm cả tiếng Việt có dấu)
- `capstone`: tìm xref tới chuỗi/IAT rồi đọc các hàm `0x63d78d`, `0x63e8e0`, `0x63fe36`, `0x6409a1`, `0x640f36`, `0x645900`, `0x64c3eb`
- Tách APK nhúng (scrcpy-server) bằng cách đọc End-Of-Central-Directory của ZIP
- **Không chạy** file .exe nào. MctHost chưa được unpack.
