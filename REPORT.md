# Phân tích 3 file: vlcmauto.exe, vlcmcliet.exe, game.pak

Đây là bộ **tool auto (bot) bên thứ ba** cho game Flash **Võ Lâm Chi Mộng (VLCM)** của 360game/Zing/TePayLink. Nó không phải client chính thức.

| File | Kích thước | SHA-256 | Loại |
|---|---|---|---|
| `vlcmauto.exe` | 1 022 976 | `98b7eebe…734967` | PE32 GUI, C++/ATL (MSVC), build 2026-08-11 |
| `vlcmcliet.exe` | 484 352 (UPX) → 831 488 | `563b81ed…2dcf4` (đã pack) | PE32 GUI, C++/ATL, **nén UPX**, build 2026-08-21 |
| `game.pak` | 309 328 | `d64f90dc…afa87a` | SWF đã mã hóa AES-256-CBC + HMAC |

Đường dẫn PDB lộ ra tên dự án của tác giả:
`K:\autovlcm\lam-skikll-mobi\vlcmauto\...` và `K:\autovlcm\lam-skikll-mobi\vlcmcliet-fix-ram-onl-mobi\...`

---

## 1. Kiến trúc tổng thể

```
vlcmauto.exe (trình quản lý nhiều acc)
   │  đọc/ghi data\accounts.xml, acc.txt, proxy.txt, proxy_map.dat
   │  tự update game.pak từ Google Drive
   └─ chạy nhiều tiến trình:
      vlcmcliet.exe --user X --pass Y --server N --channel N --player N
                    --portal (360game|zing|zplay|vuigame|tepaylink)
                    --proxyall --tabsperproxy N
         │  đăng nhập cổng game qua WinHTTP -> lấy sign/auth/loginServer
         │  giải mã data\game.pak -> SWF trong RAM
         └─ nhúng ActiveX Flash (ShockwaveFlash) -> chạy SWF
               SWF = loader game gốc + mã bot (auto train, phó bản, PK…)
```

---

## 2. vlcmauto.exe — Trình quản lý acc

Cửa sổ `VLCMAutoMain`, tiêu đề `vlcm-Train-TPL`. Chức năng suy ra từ chuỗi giao diện:

- **Quản lý nhân vật**: bảng *Tài khoản / Mật khẩu / Cổng game / Máy chủ / Kênh / Nhân vật*. Các nút Thêm, Sửa, Xóa, Đăng nhập, "Đưa cửa sổ game lên trên".
- **Lưu acc** tại `data\accounts.xml` với các thẻ `<acc><cong><user><pass><sv><kenh><nv><hidden>`.
  Mật khẩu **chỉ được làm rối bằng XOR** với chuỗi cố định `vlcmPK-enc-key` (14 ký tự, XOR theo từng ký tự UTF-16) rồi đổi sang hex (hàm tại `0x405fb3` / `0x406260`). Như vậy, ai có file này đều **giải ngược ra mật khẩu dễ dàng**.
- **Chạy lặp hàng loạt**: nạp acc từ `data\acc.txt`, mỗi lần 10 ID, đổi lô acc sau N giây ("Đang nạp 10 ID mới…", "Đã hết tài khoản trong acc.txt").
- **Tự reconnect**: đưa acc vào hàng đợi, thử lại, nghỉ 2 hoặc 10 phút. Nếu `LOGINING` treo quá 4 phút thì kill client rồi mở lại.
- **Proxy**: `data\proxy.txt` và `proxy_map.dat`, tùy chọn "Dùng Proxy cho tất cả (bỏ qua 5 tab mạng gốc)", "Số tab/Proxy".
- **Xóa cookie**: chạy `rundll32.exe InetCpl.cpl,ClearMyTracksByProcess 4351` và xóa cookie Flash.
- **Tự cập nhật game.pak** (User-Agent `vlcmPK/1.0`):
  - version: `https://drive.google.com/uc?export=download&id=1jBDzRA3Z9KBbSlLLsI4cTOqgctf8gK_T`, so sánh với `data\vs.txt`
  - link.txt: `https://drive.google.com/uc?export=download&id=1cEFo49l_7yTTLNzTu_oFaranGsPTmvS0`, chứa URL tải `game.pak.tmp` → ghi đè `game.pak`
- ⚠️ **Thêm ngoại lệ Windows Defender**: chạy ẩn
  `powershell.exe -WindowStyle Hidden -Command "Add-MpPreference -ExclusionPath '<thư mục tool>'"`
  rồi tạo `data\excluded.flag`. Manifest là `asInvoker`, nên lệnh này chỉ có tác dụng khi người dùng chạy tool bằng quyền Admin. Khi đó **toàn bộ thư mục tool bị loại khỏi quét virus**, kể cả mọi file tải về sau này qua cơ chế update.

Import: chỉ có WinHTTP (tải file), `CreateProcessW`/`TerminateProcess`/`OpenProcess` (quản lý client), `ShellExecute*`, và các hàm INI/Registry-free. Mình không thấy code keylog hay gửi mật khẩu ra ngoài trong file này.

---

## 3. vlcmcliet.exe — Flash host + đăng nhập

Sau khi `upx -d` giải nén, đây là host ATL nhúng ActiveX `ShockwaveFlash` (class `VLCMFlashFrame`). Nó có mô tả "vlcmClient - VLCM Flash Host".

### 3.1 Đăng nhập (đóng vai trình duyệt, UA giả Firefox/Chrome/Edge)
| Cổng | Endpoint |
|---|---|
| Zing | `https://sso3.zing.vn/xlogin` (apikey `848dfc7c1dfe4da3b8dd3c58f8d34be8`) |
| 360game / vuigame / zingplay / zingme | `id.vlcm.360game.vn`, `vuigame.vlcm.360game.vn`, `zingplay.vlcm.360game.vn`, `zingme.vlcm.360game.vn`: `/login-game-minilauncher`, `/server-game-minilauncher`, `/play-game-minilauncher?_svid=s…` |
| TePayLink | `https://login-vlcm.tpl.vn/api/v2/User/login`, `/api/v2/User/GetToken`, `/api/gamepage/gamepage.asp?ServerID=…&SessionId=…&Device=windows` |

Client lấy `iframe linkgame` và `var parameters` (game/auth/sign, loginServer, loginPort, chatServer, baseDir…). Từ đó nó dựng lại URL `TGameLoader.swf?loadnum=9&policys=…`, `TGame.tse`, `TColdLib.tse`, `config.tsze`… giống launcher gốc. Có xử lý khi gặp "login thất bại bị capcha".

Theo những gì thấy, credential chỉ được gửi tới **các cổng đăng nhập của game**. Mình không thấy endpoint lạ nào nhận mật khẩu.

### 3.2 Nạp game.pak
`data\game.pak` được giải mã trong RAM (thông báo lỗi "Giải mã game.pak thất bại", "SWF header không hợp lệ") rồi nạp vào Flash qua `LoadSwfViaOcxState`, với `AllowScriptAccess` và `AllowFullScreen`. Chi tiết thuật toán ở mục 4.

### 3.3 Cầu nối JS ↔ C++ (ExternalInterface)
Có các callback `allow_call_flash_function`, `load_flash_movie`, `allow_send_auto_list`, `receive_auto_list`, `load_auto_settings`, `save_state` / `load_state` / `receive_state` (lưu `data\state_*`), `check_player_info`, `load_online`, `disconnected`, `tatgame`. Danh sách auto được lưu ở `auto_list.txt`.

### 3.4 Tính năng khác
- **Auto Click** (`VLCMAutoClickGui`): F5 ẩn/hiện, F6 lấy vị trí, F7 chạy, F8 dừng, F11 fullscreen. Click tại tọa độ X/Y mỗi N ms.
- **Trim RAM**: `K32EmptyWorkingSet` / `SetProcessWorkingSetSize` ("RAM đã trim! Working Set: %zuMB"). Tên dự án "fix-ram" ứng với tính năng này.
- `CreateJobObject` / `AssignProcessToJobObject`: gom tiến trình con. Ngoài ra có `ReadProcessMemory` / `WriteProcessMemory` / `VirtualProtectEx` / `VirtualQueryEx`, nhiều khả năng dùng để vá Flash OCX trong tiến trình.

### 3.5 ⚠️ Kill-switch từ xa (được giấu)
Có một chuỗi base64 bị làm rối (base64 → đảo bit NOT):
```
iZOckqCP…  →  "vlcm_pck_2025" + https://drive.google.com/uc?export=download&id=1M70hyjG_8yqzwqtE96ayijKpmHcpu4dO
```
Luồng tại `0x41c4d0` chạy như sau:
1. Chờ 120 giây sau khi mở (`Sleep(0x1D4C0)`).
2. GET file Drive trên, thử lại 3 lần, mỗi lần cách 3 giây.
3. Nếu tải lỗi, hoặc nội dung không qua được bước kiểm tra (`0x41c0f0`, có so sánh số thực, nhiều khả năng là hạn dùng hoặc whitelist), nó gửi `PostMessage(hwnd, WM_CLOSE)` để **tự đóng client**.

Nghĩa là tác giả có thể **tắt tool từ xa** bất cứ lúc nào: một cơ chế bản quyền/hạn dùng.

---

## 4. game.pak — SWF bot đã mã hóa

### 4.1 Định dạng mã hóa
Mình dịch ngược từ hàm `0x40c6a0` của vlcmcliet.exe:

```
[0:16]   salt
[16:32]  IV
[32:-32] AES-256-CBC ciphertext (PKCS7)
[-32:]   HMAC-SHA256(key[32:64], IV || ciphertext)
key(64B) = PBKDF2-HMAC-SHA256("xQ9#hI3u!28@91992$$Lm&zW", salt, 100000)
```
Mình đã giải mã thành công (HMAC khớp). Kết quả là `CWS` = SWF v26 nén zlib, 309 263 byte (giải nén ra 648 307 byte). Script: [`tools/decrypt_pak.py`](tools/decrypt_pak.py).

### 4.2 Nội dung SWF
- Viết bằng Flex/AS3. Bị **obfuscate** kiểu secureSWF/DoSWF: tên lớp dạng `_e_-_-_--`, chuỗi bị mã hóa và giải qua `_e_-----_._e_-_-__-(int)`, control-flow rối. Có 3 `DefineBinaryData`: bảng khóa/chuỗi và một payload ~66 KB.
- Lớp `Main` (~9,8 MB mã AS3 sau decompile bằng JPEXS FFDec) chứa **toàn bộ logic bot**. Nó chạy bên trong game thật và gọi trực tiếp các đối tượng `mainChar`, `scene`, `avatarManager`, `goodsResManager`, `TCursorManager`. Nó cũng **gửi gói tin giao thức trực tiếp** qua các hàm `send_10193`, `send_10901`, `send_30001`, `send_50583`, `send_60227`…

Tính năng bot đọc được từ tên biến/hàm:

| Nhóm | Ví dụ định danh |
|---|---|
| Auto train / đánh quái | `trainMap`, `trainX/Y`, `var_anquai`, `var_auto5skill`, `train_useQuickbarFirst5Skill`, `var_doimuctieu*`, `trainF4PauseResume` |
| Auto phó bản 15→70 | `PhoBan_15…PhoBan_70`, `pb15Maps…pb70PosArrs`, `pb20Gate*`, `pb25JumpAround*`, `pb30CatThuChienId`, `pb40Door/pb40thanbi`, `nhanquapb1`, `var_tatkhipbxong1` |
| Nhặt đồ | `pickupGoods`, `pb_tryPickupLikeTrain1`, `var_PickUp_MsgReceivedProxy1` |
| PK tự động | `findNearestPkPlayer1`, `var_pkTrain`, `var_pk_ignore_names`, `var_pk_tongbang`, `pkRange`, `pkInlineSkill` |
| Vận tiêu | `vantieu_updateEscortSpeed1/2`, `vantieu_tryReturnToEscort1`, `vantieu_received_537xx` |
| Nhiệm vụ | `mainlineQuestEnabled`, `dailyQuestEnabled`, `btnDaily/Weekly`, `auto3Skill_*` (lên cấp 15 → PB15) |
| Kỹ năng / kinh mạch | `autoSkillCQ0_*` (tự nâng skill môn phái/bang/giang hồ), `autoNangMach`, `jingmai` |
| Hồi phục / sửa đồ | `var_autoNpcDrugEnabled`, `var_autoNpcRepairEnabled`, `trainRepair1638d*`, `var_buff`, `huiFu` |
| Mua bán / quà | `var_muatien*`, `buyBa`, `shop`, `nhanqua2`, `phanthuong2`, `giftId`, `isVIP` |
| Di chuyển | `var_bay*`, `var_nhay*` (bay/nhảy), `autoCatch_hook500xx`, `coBuaBeQuan`, `var_autoCoMoNhanh` |
| Khác | `send_tudanh`, `send_como*`, `var_chat*`, `tathengio` / `checkScheduledTime` (hẹn giờ tắt) |

---

## 5. Đánh giá rủi ro

| Mức | Vấn đề |
|---|---|
| 🔴 Cao | **vlcmauto tự thêm thư mục vào Exclusion của Windows Defender** (PowerShell ẩn). Kết hợp với cơ chế **tự tải file từ Google Drive do tác giả kiểm soát**, đây là rủi ro lớn: sau này tác giả (hoặc ai chiếm được Drive đó) có thể đẩy file độc mà Defender không quét. |
| 🟠 Trung bình | **Mật khẩu game lưu gần như plaintext** (XOR khóa cố định `vlcmPK-enc-key` + hex) trong `data\accounts.xml`. Ai lấy được file này là có mật khẩu. |
| 🟠 Trung bình | **Kill-switch từ xa** được giấu (link Drive bị làm rối), 2 phút sau khi mở sẽ kiểm tra. Tool có thể ngừng hoạt động bất cứ lúc nào. |
| 🟠 Trung bình | vlcmcliet bị **pack UPX**, game.pak bị **mã hóa + obfuscate**. Người dùng không kiểm chứng được code chạy. Tuy vậy, trong phạm vi đã phân tích, mình **không thấy** hành vi gửi mật khẩu tới server lạ, keylogger hay đào coin. |
| ⚖️ | Đây là **bot/hack game**, vi phạm điều khoản của nhà phát hành. Acc có nguy cơ bị khóa. Tool còn gửi gói tin trực tiếp (packet-level), nên dễ bị phát hiện hơn auto-click. |

**Khuyến nghị:**
- Nếu đã chạy vlcmauto bằng quyền Admin, kiểm tra lại bằng `Get-MpPreference | Select -Expand ExclusionPath` và xóa ngoại lệ bằng `Remove-MpPreference -ExclusionPath '<path>'`.
- Đổi mật khẩu các acc đã từng lưu trong tool.
- Chỉ chạy tool trong máy ảo hoặc sandbox.

---

## 6. Phương pháp
- `file`, `pefile` (header, import, timestamp), `upx -d` (giải nén client)
- `strings` ASCII + UTF-16LE (chuỗi tiếng Việt)
- `capstone` disassemble các hàm gọi `BCryptDeriveKeyPBKDF2` và `CryptStringToBinaryA`, đọc thuật toán và hằng số
- Python `hashlib`/`pycryptodome` giải mã game.pak; JPEXS FFDec 24.1.1 decompile SWF

Mình không đưa SWF đã giải mã và mã decompile vào repo, vì đó là mã nguồn game có bản quyền.
