# Phân tích 3 file: AutoVLCMO.exe, MctHost.exe, Datas.rar

Bộ này **khác hẳn** bộ `vlcmauto.exe / vlcmcliet.exe / game.pak` trong [REPORT.md](REPORT.md). Đây là **auto thương mại của 360AUTO** (Công ty CP Công nghệ MOBOT, Huế), bán theo gói VIP. Nó chạy game trên **giả lập Android** hoặc trên **bản PC** qua `MctHost.exe`.

| File | Kích thước | SHA-256 | Loại |
|---|---|---|---|
| `AutoVLCMO.exe` | 5 922 936 | `b3301ec0…ab401a4a` | PE32 GUI, MFC/C++ (MSVC), build 2026-09-24, v1.0.7.2 |
| `MctHost.exe` | 5 014 536 | `6b24fa7d…42c34986` | PE32 GUI, **pack VMProtect** (đã dump), build 2026-06-26 |
| `Datas.rar` | 10 089 | `9ce8ae66…31350686` | RAR5, 13 file XML cấu hình (2026-04-21) |

Cả hai file .exe đều có **chữ ký số EV hợp lệ** (GlobalSign GCC R45 EV CodeSigning CA 2020) cấp cho
`MOBOT TECHNOLOGY JOINT STOCK COMPANY`, MST 3301636877, 25 Đồng Khởi, P. Thủy Xuân, Huế, email `congtymobot@gmail.com`.

Thông tin phiên bản của AutoVLCMO: `CompanyName = http://360auto.vn`, `FileDescription = Auto Võ Lâm Chân Mệnh Origin`, `InternalName = Mobot`, `ProductName = Mobot Framework`.

---

## 1. Kiến trúc tổng thể

```
AutoVLCMO.exe (giao diện + bộ điều khiển, "Mobot Framework")
 ├─ Settings\Accounts.json, AppConfig.json, Statistics.db (SQLite), UserData\Users.dat
 ├─ Mobot.mpk  (gói MPK: chỉ dữ liệu JSON, xem mục 5)
 ├─ Chế độ giả lập: Nox / MEmu / BlueStacks 5 / LDPlayer
 │     └─ điều khiển qua ADB, đẩy scrcpy-server.jar để lấy hình + gửi thao tác
 ├─ Chế độ PC:  GameHost\MctHost.exe --bootdir . -instance:<GUID>
 │     └─ tự login Tepaylink, nạp Adobe AIR + client game chính thức + ember.dll (Lua + máy ảo MVM = logic bot), cửa sổ "Mộng Chí Tôn"
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
- Liên quan: `Flash.ocx`, `Engine.dat`, `register_flash_new.bat`, `manifest.json` (danh sách `files` để kiểm tra hoặc cập nhật). Các chuỗi này có trong AutoVLCMO, nhưng MctHost bản này dùng Adobe AIR chứ không dùng Flash.ocx (xem mục 3), nên có thể là phần sót lại từ bản cũ.
- Tự tìm và đóng hộp thoại lỗi `#32770` của MctHost, và kill `UnityCrashHandler64.exe`.

### 2.4 Bản quyền / VIP
- Kích hoạt bằng **mã VIP** (`MENU_VIP_ACTIVEKEY`), mua ở `vip.360auto.vn` (`/redirect/%s?redirect_uri=/keylisting`, `/shop/%d`).
- **Mã máy (HWID)** tạo ở hàm `0x645900`: code tự sửa (self-modifying) để gọi `CPUID` với nhiều leaf, trộn với chuỗi `twElsZQgwN` và hằng `0x19780102`.
- Kiểm tra **giờ hệ thống** với server. Nếu giờ máy lệch thì bắt chỉnh lại, nhằm chặn việc lùi giờ để kéo dài hạn VIP.
- `Mobot.mpk` hoá ra chỉ chứa dữ liệu JSON; logic thật nằm trong `ember.dll` (xem mục 5).

### 2.5 ⚠️ MobotRemote: xem và điều khiển màn hình từ xa
Có một module hoàn chỉnh `MobotRemote` (class `IScreenProvider@MobotRemote`), UA `MobotRemote/1.0` và `MobotRemote-Video/1.0`:

- Mở WebSocket tới `/ws/desktop?role=app&machine=<HWID>&product=<id>&uid=<user>&sdk=<ver>` với `Authorization: Bearer <token>`. Tên máy chủ **không lộ ra dưới dạng chuỗi tĩnh** (được truyền vào lúc chạy; không có trong `Mobot.mpk`).
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

### 3.1 Vỏ bọc
**Pack bằng VMProtect 3.x**: section `.text`, `.rdata`, `.data`, `.23e` có kích thước thô bằng 0, code nén nằm trong `.kG0` (4,9 MB, entropy 7.93). Phân tích tĩnh không đọc được gì ngoài import.

### 3.2 Dump bộ nhớ (phân tích động)
Container không có KVM nên không dựng được máy ảo Windows. Mình chạy file bằng **Wine 9.0 + Xvfb** ngay trong container dùng một lần này, rồi đọc `/proc/<pid>/mem` vùng `0x400000–0xB82000` ([`tools/dump_wine.py`](tools/dump_wine.py)).
- Sau **0,5 giây**, VMProtect đã giải nén xong: `.text` 89 % byte khác 0, `.rdata` có đủ chuỗi. VMProtect **không chặn Wine**.
- Tiến trình tự thoát (rc=254) vì thiếu tham số `--login`/`--no-login` hoặc thiếu Adobe AIR. Nhưng lúc đó code đã được giải nén nên vẫn đọc được.
- Bản dump **không đưa vào repo**.

### 3.3 MctHost thật sự làm gì
Đây **không phải host Flash OCX** như mình đoán ở bản trước. Nó là **launcher nạp client Adobe AIR chính thức của game "Mộng Chí Tôn" vào trong chính tiến trình**:

```
Usage: MctHost.exe (--login user:pass@server | --no-login)
       [--gamedir <path> | --bootdir <phase4_bundle>] [--ember <dll>] [--no-ember]
       [--runtime <dir>] [--log-dir <path>]
```

1. **Tự đăng nhập Tepaylink** (`DoLoginTepayLink`), UA giả `Firefox/34.0`:
   - step1 `POST https://login-vlcm.tpl.vn/api/v2/User/GetToken` với `{"password":…,"username":…,"device":"Windows"}` → `token`
   - step2 `POST …/api/v2/User/Login` với `{"username":…,"device":"Windows","token":…}` → `sessionId`
   - step3 `GET …/api/gamepage/gamepage.asp?ServerID=<sv>&SessionId=<sid>&Device=windows` → tách `embedSWF("…")` và khối `parameters = {…}` ra URL SWF và flashvars
   - Kết quả được truyền vào game qua biến môi trường `EMBER2_SWF_URL`, `EMBER2_FLASHVARS`, `EMBER2_LOG_DIR`.
2. **Nạp Adobe AIR trong tiến trình**: `LoadLibraryW("Adobe AIR\Versions\1.0\Adobe AIR.dll")` rồi gọi thẳng `CaptiveAppEntryWinMain`, với thư mục game mặc định `D:\Games\MongChiTonClient` (`--gamedir`) hoặc bundle đã sửa (`--bootdir`, "phase4", có `boot.swf`).
3. **Nạp `ember.dll`**: đây là DLL/ANE (AIR Native Extension) của Mobot, chạy bên trong game. Đây chính là engine bot (xem mục 5).
4. **Bẻ giới hạn 1 cửa sổ game** bằng **MinHook**: hook `CreateMutexA`/`CreateFileMappingA` để đổi tên `MacromediaMutexOmega`/`MacromediaFMOmega` thành `…_pid<N>` khi phát hiện MctHost khác đang chạy. Nhờ vậy chạy được nhiều client cùng lúc.
5. **Đổi giao diện cửa sổ**: hook `CreateWindowExW` và chờ class `ApolloRuntimeContentWindow` để đặt tiêu đề, icon và nút riêng ("skin").
6. Ghi log ra `<log-dir>/host.log`. Log **không ghi mật khẩu** (chỉ ghi user/server và độ dài body). Nhưng log **có ghi `token`, `sessionId`, flashvars và 500 ký tự đầu của phản hồi server**. Ai đọc được `host.log` là có thể dùng lại phiên đăng nhập.

Mật khẩu **chỉ được gửi tới `login-vlcm.tpl.vn`** (cổng chính thức). Mình không thấy URL nào khác trong bản dump.

Lưu ý: AutoVLCMO gọi `"MctHost.exe" --bootdir . -instance:{GUID}`, nhưng trong bản này MctHost coi `-instance:` là tham số lạ (`WARN: ignoring unknown arg`) và bắt buộc phải có `--login`. Vậy nên hai file có thể lệch phiên bản, hoặc AutoVLCMO còn thêm `--login` ở chỗ khác.

## 4. Datas.rar: dữ liệu kịch bản

| File | Nội dung |
|---|---|
| `Servers.xml` | 734 máy chủ (`gate="vlcm"`, id 1 Kim Long … 734 Hắc Lâm). **Không phải XML hợp lệ**: không có thẻ gốc, có BOM |
| `Maps_PB.xml`, `Maps_Pro.xml` | Bảng `id → tên bản đồ` + `cutline` (id 20001–20280: Tương Dương, Cổ Mộ, Chung Nam Sơn, Tuyệt Tình Cốc, Viên Nguyệt, Địa Cung, Chốn Hư Ảo, Bạch Đà Sơn Hạ…). Bản Pro có thêm Cổ Mộ Mật Thất 2–4 và các map cutline 15–16 |
| `Skills.xml` | ID kỹ năng theo nhóm: Thiếu Lâm/Toàn Chân/Cổ Mộ/Đào Hoa (3129–3168), Giang Hồ, buff (Cách Không Độ Khí 3164, Từ Hàng Phổ Độ 3147…), thú chiến (Hồi Sinh 3216…), bị động, công pháp Thần Chiếu Kinh 99201. **Lỗi dữ liệu**: Bạch Đà và Minh Giáo dùng lại y hệt ID 3133/3135/3161 của Đào Hoa (copy-paste chưa sửa) |
| `Drugs.xml` | Thuốc HP 30101–30104 (Quy/Thủy/Bích/Thần Đơn), MP 30201–30204 (Ninh/Tuyết/Vân/Bách Hoàn) |
| `Redirect.xml` | Đổi đích click: tọa độ NPC ở Tương Dương (20002) → ID NPC cửa hàng: 1638 Vũ Khí, 1639 Thuốc, 1640 Trang Sức, 1642 Gấm Vóc, 1647 Thú Cưỡi, 1650 Bí Kíp, 1090 Tiểu Nhị, 1739 Tạp Hóa. Ngoài ra còn 3 điểm đổi tọa độ quái (Loạn Quân, Thương Binh/Kỵ Binh Phản Bội) |
| `LienTram.xml`, `ThienQuan.xml`, `DoanhTrai.xml`, `DoanhTrai_5_15.xml`, `PhongAnTran.xml`, `MeCung15.xml`, `MeCungThanBi.xml` | Chuỗi tọa độ `<Point x y>` để **chạy vòng gom/quây quái** trong từng phó bản. Chú thích đầu file của Doanh Trại vẫn ghi "Liên Trảm" (copy-paste) |

Không file .exe nào ở đây tham chiếu tên các file XML này. Bản hiện tại dùng JSON trong `Mobot.mpk` thay cho chúng (cùng loại dữ liệu), nên nhiều khả năng Datas.rar là dữ liệu của bản auto cũ hơn. ID NPC 1638 trùng với `trainRepair1638` của bot SWF ở [REPORT.md](REPORT.md), tức là cả hai tool cùng chạy trên một game.

---

## 5. Mobot.mpk và ember.dll

| File | Kích thước | SHA-256 | Loại |
|---|---|---|---|
| `Mobot.mpk` | 3 262 | `c74950ee…ad3e2f07` | Gói MPK v2, 8 file |
| `ember.dll` | 13 862 520 | `31505053…b644389c` | PE32 DLL, build 2026-09-24, **ký EV của MOBOT**, export `ExtInitializer`/`ExtFinalizer` (Adobe AIR Native Extension) |

### 5.1 Định dạng MPK: đã giải được
Mình dịch ngược từ hàm ghi gói của AutoVLCMO (`0x6363e3`), rồi viết [`tools/unpack_mpk.py`](tools/unpack_mpk.py):

```
header 28 byte : "MPK\0" u16 ver=2 … u32 entry_size=40, u32 count, u32 table_off, u32 data_off
entry  40 byte : u64 name_hash, md5[16], u32 flags, u32 raw, u32 stored, u32 offset
flags 0x080    : nén zlib
flags 0x100    : XOR lặp với md5[16], mà MD5 này nằm ngay trong bảng mục lục => không có khóa bí mật
```
Giải được **100 % file (MD5 khớp)** ở cả hai gói. Tên file không được lưu, chỉ có hash 64-bit.

### 5.2 Mobot.mpk: chỉ là dữ liệu
8 file JSON: vật phẩm cần nhặt/dùng (Long Thú Cân, Bích Linh Đơn…), 8 kinh mạch, skill môn phái (id 51011…, kèm môn phái), skill giang hồ/khác (52001…, 88001 Giáng Long Thập Bát Chưởng…), danh sách boss (id → tên: Hoắc Đô, Lý Mạc Sầu…), danh sách vật phẩm giao dịch, thuốc theo cấp. **Không có logic.**

### 5.3 ember.dll: engine bot chạy trong client game
- Nhúng **Lua 5.3.5** (bản 32-bit, có sửa đổi) và **OpenSSL/libcurl**.
- Cầu nối Lua ↔ AS3 qua FRE API: `ember.callAS`, `newAS`, `readPrivate` (đọc cả thuộc tính **private** của lớp game), `pairs_trait`, **`ember.hook` / `ember.onAbcCall`** (chặn lời gọi hàm ActionScript của game), `ember.watch`, `ember.stage`. Nó cũng giả lập phím/chuột bằng `PostMessage` (log "INPUT: nut %d qua han -> TU NHA").
- Lua có đủ thư viện chuẩn, **gồm cả `os.execute`, `io.popen`, `io.open`, `loadlib`**. Về kỹ thuật, script tải về có thể chạy lệnh hệ thống và đọc/ghi file.
- Lộ đường dẫn phát triển: `d:/Mobot/SwfLua/ember2/scripts/ember2_tests.lua`.
- Code nhân viết bằng C++ được **obfuscate kiểu OLLVM** (control-flow flattening, hằng số giả). Phần thư viện Lua thì không bị obfuscate.

### 5.4 Gói MPK nhúng trong ember.dll (offset `0x75f608`): 99 file Lua 5.3 bytecode
Mình decompile cả 99 file bằng **unluac** (không file nào lỗi, ~40 000 dòng). Chia làm 3 loại:

| Loại | Số file | Nội dung |
|---|---|---|
| Lua đọc được | 25 | Khung AI: `ActivityDefine` (id hoạt động 2000–2100: train, NV chính/phụ/ngày/tuần/tuần hoàn, PB Liên Trảm/Thiên Quan/Doanh Trại/Mê Cung/Phu Tử, vận tiêu, bắt ngựa, giao dịch, tiệc cưới, sóc báu, boss, 3v3, giftcode, đổi acc), `ActivitySettings`, `ActivityManager`, `Goal`/`GoalComposite`/`StateMachine`/`Trigger`/`TriggerManager`, `EventDispatcher`, boot script `ember2` (đăng ký handler AS3, `ENTER_FRAME`); thư viện: JSON, LibDeflate, CRC32, md5.lua, fnv1a32, đổi mã TCVN3↔UTF-8, `NameData` (~25 000 tên tiếng Anh để **tự đặt tên nhân vật**), class `WebBot` (HTTP GET/POST qua **lcurl**, UA Chrome giả, có `PinSSL`) |
| **Máy ảo MVM** | 73 | Toàn bộ logic từng chức năng (`AutoAI.*`). Mỗi file chỉ có dạng `AutoAI.Deflate(<blob>)`: blob = raw-deflate → định dạng **`MVMP` v2** (7 MB sau giải nén) |
| MVM gốc | 1 | File lớn nhất (357 KB) gọi `mvm.run_vm_compressed(<blob>)`, cũng là MVMP |

**MVM là gì:** `run_vm` (`0x106450e0`) đọc MVMP (`0x10647e20`: bảng hằng có tag, bảng chuỗi, hàm con đệ quy), rồi dựng một **Lua proto giả chỉ chứa lệnh `RETURN`**. Proto này gắn con trỏ tới cấu trúc MVM vào trường tuỳ biến `+0x50` và đặt cờ `+0x4c`. Lua VM trong ember đã bị sửa để khi gặp cờ đó thì chuyển sang **trình thông dịch riêng** (`0x10648d10`, **46 KB trong một hàm**, 21 bảng nhảy). Hằng chuỗi trong MVMP **được mã hoá** và chỉ giải mã lúc chạy.

=> Chưa có devirtualizer nên **chưa đọc được luồng lệnh** (thuật toán, thứ tự gọi). Nhưng **bảng chuỗi thì đã giải được toàn bộ** (mục 5.5).

### 5.5 Giải mã chuỗi trong MVM
Hàm giải mã chuỗi nằm ở `0x106476c0`. Nó được gọi lười (lazy) từ 15 chỗ trong trình thông dịch, và kết quả được cache lại. Thuật toán:

```
seed = f20 - fmod(nK*7 + f10*13 + (f14 != 0)*31, 2^20)      (0x10648950 chỉnh seed)
x    = fmod(seed*65537 + idx*8191, 2^31)                     (idx tính từ 1)
mỗi byte i:  x = LCG(x); a = floor(x/2^16) & 255
             x = LCG(x); c = floor(x/2^8)  & 255
             k = (a + 7 + 7i + 3c) & 255  (0 -> 0xAD);   out = in XOR k
LCG(x) = fmod(x*1103515245 + 12345, 2^31)   (hằng số rand() của glibc, tính bằng double)
```
`f20`, `f10`, `f14`, `nK` đều nằm ngay trong header của từng hàm MVMP, nên **không có khóa bí mật**. Công cụ: [`tools/mvmp_strings.py`](tools/mvmp_strings.py) (kèm parser bố cục MVMP đầy đủ).

Kết quả: giải được cả **74/74 file MVMP**, tổng **24 082 chuỗi (5 449 chuỗi khác nhau)**, tất cả đều là văn bản có nghĩa. Ngoại lệ duy nhất là bảng mã TCVN3 và một ít dữ liệu nhị phân trong module lõi. Những gì đọc được:

- **Máy chủ:** URL duy nhất là 3 endpoint đăng nhập chính thức `login-vlcm.tpl.vn` (`GetToken`, `Login`, `gamepage.asp`), dùng trong module đăng nhập (`user`/`pass`/`server` → `loginTepayLink` → `WebBot`). **Không có URL, IP hay tên miền lạ nào khác.**
- **`os`/`io`:** không thấy `os.execute`, `io.popen`, `loadstring` hay `dofile` trong bảng chuỗi. `execute` chỉ là tên trường trong bảng luật tổ đội.
- **Liên lạc với AutoVLCMO:** qua `SendBroadcastJson(MSG_GAME_RESPONSE, …)`, `SendActivityLog`, `SendGameStatus`, `SendPlayerInfo`, `HandleMessage`. Dữ liệu gửi về là vị trí, đồ trong túi, skill, bạn bè, vợ/chồng, thông tin nhân vật. Đây là để hiển thị trên giao diện auto (native `AutoAI::HandleMessage`/`OnNetworkDisconnect` trong ember).
- **Bản quyền:** `License`, `HasValidLicense`, `UpdateLicense`, `MSG_GAME_LICENSE`, `MSG_LICENSE_SIGNATURE`, `SetAuthorizationToken`, `GetEngineTicket`. Engine chỉ chạy khi AutoVLCMO gửi xuống giấy phép có chữ ký (ember có import `CryptSignHash`/kho chứng chỉ).
- **Can thiệp game:** gọi thẳng **61 hàm gửi gói `send_<cmd>`** của client (vd. `send_10051`, `send_50041`, `send_53739`…) và `sendMsg`. Dùng **khoảng 50 lớp AS3 `com.tgame.*`** (`TSocket`, `FacadeManager`, `Item_BackpackMediator`…), cài hook `InstallChatHook`, `InstallInviteApplyHooks`, `InstallGuildListHook`, `SetupHideHooks` (ẩn người chơi/hiệu ứng), `SetupAddBuffPipeHook`. Có AutoPK và `WMKeyDown` (giả lập phím).
- **Quảng cáo:** 3 câu "360auto - chúc ngày mới vui vẻ!", "360auto - chúc các đại hiệp chơi game vui vẻ!" và "Tải auto tại 360auto.net nha mọi người!" được chọn ngẫu nhiên theo ngày rồi đưa qua `SendSystemChat`. Đây cũng là hàm dùng để báo "Nhặt được %s…", nên nhiều khả năng **chỉ hiện ở kênh hệ thống trên máy người dùng**, không gửi lên kênh chat chung. Tuy vậy, chưa có devirtualizer nên chưa khẳng định tuyệt đối.
- Không thấy chuỗi nào liên quan đến né GM, captcha hay chống phát hiện.

---

## 6. Đánh giá rủi ro

| Mức | Vấn đề |
|---|---|
| 🟠 Trung bình | **MobotRemote**: server của hãng có thể xem màn hình và gửi thao tác vào giả lập/game, kèm mã máy (HWID). Chưa xác định được lúc nào module này hoạt động. |
| 🟠 Trung bình | **Vá `HD-Player.exe` của BlueStacks trên đĩa** và bật ADB. Làm hỏng chữ ký số, và có thể gây lỗi khi cập nhật BlueStacks. |
| 🟡 Thấp | Logic bot nằm trong **máy ảo MVM** tự chế trong `ember.dll`. Lua có sẵn `os`/`io` và HTTP client. **Đã giải mã toàn bộ 24 082 chuỗi**: chỉ gọi tới cổng đăng nhập chính thức, không thấy `os.execute`/`io.popen`, không có máy chủ lạ. Luồng lệnh thì chưa devirtualize. MctHost bị VMProtect nhưng đã dump được: nó chỉ đăng nhập qua cổng chính thức, nạp AIR và bẻ giới hạn nhiều cửa sổ. |
| 🟡 Thấp | `host.log` của MctHost ghi **token và sessionId Tepaylink** dạng rõ. |
| 🟡 Thấp | Ping `whos.amung.us` (theo dõi số người dùng), ghi đè command line tiến trình con, cắt kết nối TCP. Đều là tính năng phục vụ auto, nhưng là hành vi "nhạy cảm". |
| 🟢 | Có chữ ký EV của một công ty có đăng ký thật tại VN. Không thấy thêm ngoại lệ Windows Defender (khác bộ cũ), không thấy keylogger hay đào coin. Chạy giả lập bằng token Medium IL là kỹ thuật **hạ quyền**, không phải leo thang quyền. |
| ⚖️ | Vẫn là **bot game** (farm nhiều acc, gom đồ bằng giao dịch, auto PK), vi phạm điều khoản của nhà phát hành và có nguy cơ bị khóa acc. |

**So với bộ cũ (`vlcmauto`/`vlcmcliet`/`game.pak`):** bộ này chuyên nghiệp hơn (ký số, VIP, remote, hỗ trợ 4 loại giả lập) và không có mánh thêm ngoại lệ Defender hay kill-switch qua Google Drive. Bù lại, nó kiểm soát từ xa nhiều hơn (remote screen/input, HWID, kiểm tra giờ server) và mã lõi bị giấu kỹ hơn (VMProtect + `Mobot.mpk`).

**Khuyến nghị:** chỉ chạy trong máy ảo hoặc máy riêng cho game. Không đăng nhập acc quan trọng. Nếu dùng BlueStacks, sao lưu `HD-Player.exe` trước khi chạy.

---

## 7. Phương pháp
- `unrar`, `file`, `sha256sum`, `pefile` (section, entropy, import, version, tài nguyên), `openssl pkcs7` (chứng chỉ)
- Trích chuỗi ASCII và UTF-16LE (gồm cả tiếng Việt có dấu)
- `capstone`: tìm xref tới chuỗi/IAT rồi đọc các hàm `0x63d78d`, `0x63e8e0`, `0x63fe36`, `0x6409a1`, `0x640f36`, `0x645900`, `0x64c3eb`
- Tách APK nhúng (scrcpy-server) bằng cách đọc End-Of-Central-Directory của ZIP
- MctHost: chạy bằng Wine 9.0 + Xvfb trong container, dump `/proc/<pid>/mem` sau 0,5 giây ([`tools/dump_wine.py`](tools/dump_wine.py)). AutoVLCMO **không chạy**.
- Mobot.mpk / ember.dll: dịch ngược hàm ghi MPK của AutoVLCMO, giải bằng [`tools/unpack_mpk.py`](tools/unpack_mpk.py); decompile Lua 5.3 bằng unluac (2023-12-24); parser Lua-constant tự viết để lấy blob `AutoAI.Deflate`; raw-inflate ra MVMP; đọc `run_vm`/parser/trình thông dịch MVM bằng capstone. Chuỗi MVM giải bằng [`tools/mvmp_strings.py`](tools/mvmp_strings.py) (dịch ngược `0x106476c0` và `0x10648950`). Mã Lua đã decompile và chuỗi đã giải **không đưa vào repo**.
