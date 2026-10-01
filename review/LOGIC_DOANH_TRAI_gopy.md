# Góp ý cho `LOGIC_DOANH_TRAI.md` (bản 01-10c)

Đối chiếu với logic Doanh Trại (`PhoBan_25`) của bot gốc trong `game.pak`. Mình đọc lại **P-code** của hàm này, vì bản decompile bị làm rối luồng điều khiển.

Ký hiệu:
- ✅ khớp bot gốc
- ⚠️ khác bot gốc, nên xem lại
- ❓ bot gốc không có, mình không kiểm chứng được

---

## A. Những chỗ nên sửa (ưu tiên cao)

### A1. Điều kiện sang ải (mục 4, bước 9): bot gốc chặt hơn nhiều

Tool của bạn sang ải khi có **một trong ba**: (a) 10300 cổng mở, (b) hết một vòng không gặp quái, (c) thấy cổng **và** 2 điểm tuần liên tiếp không có quái. ⚠️

Bot gốc (đọc từ P-code) làm như sau:

| Bot gốc | Chi tiết |
|---|---|
| **Cổng mở** (`var_600`) | Bật khi nhận **10300 đúng `transId` = map hiện tại, `isOpen = 1`**, *hoặc* khi **hết một vòng tuần** mà nhìn thấy cổng (`type == 7`, `usable`, `res.mapID == pb25Maps[ipb25+1]`). Bot gốc **chỉ kiểm cổng nhìn thấy ở cuối vòng tuần**, không kiểm sau 2 điểm. |
| **Sạch quái** | Không còn quái (`type == 2`) nào **còn sống trên toàn map**. Bot gốc **không giới hạn bán kính**, khác với 20 ô của bạn. |
| **Sạch đồ** | Không còn vật rơi (`type == 11`) hợp với luật nhặt (`method_190`). |
| **Đợi ổn định** | Sạch quái + sạch đồ phải giữ **liên tục 2 giây** (`pb25DwellStartMs`, 2000 ms) mới tính. |
| **Sang ải** | Cổng mở **VÀ** sạch quái **VÀ** sạch đồ **VÀ** đã giữ 2 giây: log "[PB25] Cổng đã mở + sạch quái + sạch đồ -> qua ải kế tiếp." rồi `method_360()`. |
| **Dự phòng** | Hết vòng tuần mà cổng vẫn chưa mở: tăng `var_500`. **Đủ 5 vòng** (`var_500 ≥ 5`) thì vẫn sang ải. |

So với tool của bạn:
- (b) "hết **1** vòng không gặp quái là sang": bot gốc cần **5 vòng**, và chỉ dùng khi không có tín hiệu cổng. Với 1 vòng, tool dễ bỏ ải sớm khi quái còn ở ngoài tuyến tuần. Đây đúng là rủi ro bạn tự ghi ở 11.2.
- (c) "thấy cổng + 2 điểm không quái": bot gốc chỉ kiểm cổng nhìn thấy ở **cuối vòng**, và vẫn phải sạch quái **toàn map** + sạch đồ.
- "Yên 3 giây trong 20 ô": bot gốc dùng **2 giây, sạch toàn map**.

**Đề xuất sửa mục 4, bước 9:**
```
Được sang ải khi:
  cổng mở  = (10300 đúng transId == map hiện tại, isOpen == 1)
             HOẶC (cuối vòng tuần: thấy type 7, usable, res.mapID == map ải kế)
  VÀ không còn quái sống nào trên toàn map
  VÀ không còn túi đồ cần nhặt
  VÀ trạng thái "sạch" giữ liên tục 2 giây.
Dự phòng: đủ 5 vòng tuần mà cổng vẫn chưa mở -> vẫn đi sang ải.
```

### A2. Lọc 10300 theo `transId` (mục 11.1)

Giống Thiên Quan: handler 10300 của bot gốc chỉ bật cờ khi `transId == mapID hiện tại && isOpen == 1`. Nên sửa đúng như vậy.

### A3. Map 20062 / 20067 có xử lý riêng, không chỉ là "ải được nhảy" (mục 1, mục 5)

Trên hai map này, ở **mỗi nhịp** của nhánh đánh, bot gốc làm thêm ba việc ngoài nhảy:
1. Gửi **11163** `[double getTimer()][int -1]`. Không có giới hạn tần suất, tức gửi theo từng frame.
2. Bật auto treo máy gốc của game theo cài đặt (`method_89`, gói 50583) nếu đang lệch.
3. Ép `afkInfo2.isAutoPickUpMoney = 1` (tự nhặt tiền) và `afkRangeCount = 50` (phạm vi treo máy 50), rồi lưu cấu hình lên server (`method_197`, gói **50581**).

Bot gốc cũng có một hàm xử lý gói **11162** (`§><§`), là gói server báo rơi đồ:
- Nội dung đọc được: `[int monsterID][int bagID][short x][short y][int goodsID][int amount][byte quality]`.
- Hàm này gửi ngay **11163** `[double getTimer()][int bagID]`, tức **nhặt túi đồ theo bagID mà không đi tới**. Sau đó nó chuyển gói cho `PickUp_MsgReceivedProxy.received_11162` để game xử lý bình thường.
- Mình chưa xác định được hàm này được đăng ký ở đâu và bật khi nào.

**Suy luận** (chưa kiểm chứng trên game thật):
- Bot gốc gắn "tự nhặt tiền + phạm vi 50 + nhặt mọi túi (bagID = -1)" với đúng **20062 / 20067**. Vì vậy rất có thể **ải chuột (rơi đồng) chính là 2 map này**.
- Việc nhảy quanh quái cũng chỉ bật ở 2 map đó, có lẽ để đuổi chuột.

**Đề xuất:**
- Khi tool nhận ra "ải chuột" theo tên, hãy **log map ID** để xác nhận giả thuyết trên. Nếu đúng, có thể gán ải chuột theo map ID, tin cậy hơn so tên.
- Ở ải chuột, tool đang **chờ 11162 rồi đi tới nhặt**. Bot gốc thì gửi thẳng 11163 `[time, bagID]`. Đây là cách nhặt từ xa: nhanh hơn, nhưng **dễ bị server phát hiện** nếu server có kiểm khoảng cách. Nếu muốn dùng, chỉ nên làm dự phòng khi đi tới mà không nhặt được.
- Ghi rõ trong tài liệu: **11162 = server → client** (rơi đồ), **11163 = client → server** (nhặt).

### A4. Tầm nhảy (mục 5)

| | Bot gốc | Tool của bạn |
|---|---|---|
| Tham số `maxDist` của `charJump` | `jumpMax` = `OtherConst.JUMP_MAX_DIS` (mặc định 8) | **500** ⚠️ |
| Lọc điểm rơi theo tầm | **Có**: bỏ điểm có `distance(nhân vật, điểm rơi) > jumpMax` | Không thấy ghi |

→ Nên dùng `JUMP_MAX_DIS` của game và lọc điểm rơi ngoài tầm trước khi gọi `charJump`. Truyền 500 có thể làm client gửi cú nhảy xa bất thường, hoặc bị từ chối liên tục. Bạn đã ghi ở 11.5 là "game tự từ chối, thử lại sau 0,5 giây"; lọc trước sẽ tránh được phần này.

Các điều kiện bạn thêm (thể lực `ppNow ≥ 20`, `allowJump`, `isSoft`) bot gốc **không có**. Giữ lại là tốt, chỉ cần ghi rõ đó là phần riêng của tool.

### A5. Gói vào phó bản 10701 (mục 3)

Giống Thiên Quan: `[int 1656][int 20060][byte 1][byte cấp]`, với cấp 1 = Sơ, 2 = Trung, 3 = Cao. Bot gốc mặc định 3.
- `[1656, 20060, 1, 1]` = Phó Bản **sơ**: ✅ đúng là "độ khó thấp nhất".
- Byte thứ 3 bot gốc luôn gửi 1. "Thưởng thường" là ❓ chưa kiểm chứng.

### A6. Giữ mục tiêu và bán kính (mục 4, bước 8)

Bot gốc chọn quái **gần nhất trên toàn map** (`var_460 = 999999`). Mục tiêu cách > 3 ô thì đi tới. Có thêm chống kẹt: nhân vật đứng một ô quá **800 ms** trong lúc đang đi thì gửi lại lệnh đi tới quái (`pb25StuckSinceMs`).

→ Bán kính 20 ô của bạn có thể bỏ sót quái, nên mới cần nhiều vòng tuần. Mục 11.7 "Chạy ra giữa bản đồ với các ải dưới N" cũng đi theo hướng này. Nên làm tùy chọn **tìm quái toàn map**, và thêm chống kẹt 800 ms.

---

## B. Chỗ đã khớp bot gốc ✅

| Mục | Ghi chú |
|---|---|
| Danh sách 20 map và thứ tự | ✅ `pb25Maps`; bot gốc sang ải bằng `ipb25 = vị trí map hiện tại + 1` theo đúng thứ tự này |
| Tuyến 8 điểm | ✅ trùng `pb25PosArrs` (dựng lại từ P-code) |
| Một tuyến cho 20 ải (11.3) | ✅ bot gốc **cũng chỉ có một tuyến**, nên đây không phải điểm yếu riêng |
| Đứng chờ **1 giây** ở mỗi điểm tuần | ✅ `pb25DwellAtIndex` / 1000 ms |
| Ải nhảy 20062, 20067 | ✅ |
| 12 hướng nhảy, xoay 1–3 bước ngẫu nhiên, cách nhau ≥ 0,5 giây, quái ≤ 7 ô, không nhảy khi đang đi / nhảy / nhảy 2 / nhảy 3, `isMainCharCanMove` | ✅ khớp từng điều kiện |
| Nhảy trước, nhảy được thì bỏ lượt đánh | ✅ |
| `clear()` rồi `charJump(..., -1, max, null, false, false)` | ✅ ngoại trừ giá trị `max` (xem A4) |
| `mainCharWalk("<map ải kế>,-1,-1,0")` | ✅ |
| 10726 → 10723 nhận thưởng → ra | ✅ bot gốc gửi 10723 ngay và chuyển chế độ ra sau 4 giây; ra bằng cổng `type 7` có `res.mapID == 20002`, tới trong 5 ô rồi gửi **10051** `[x,y]` (dùng được làm dự phòng) |

---

## C. Bot gốc không có, mình không kiểm chứng được ❓

- Hoa hồng: bot gốc **không kiểm hoa** ở Doanh Trại. Câu hỏi qua ải 10052 / FAlert ở Doanh Trại cũng không có trong `PhoBan_25`; bot gốc chỉ xử lý 10052 cho Thiên Quan. Nếu Doanh Trại có hộp hỏi khi qua cổng thì phần của bạn là phần mới.
- Nhận diện ải chuột theo tên "chuột" (xem giả thuyết ở A3).
- Hồi sinh / gói 20075, mở lại client.

---

## D. Góp ý nhỏ

1. **Mục 4, bước 5 (sang ải mới):** đặt lại cả hướng nhảy (`pb25JumpAroundDir`), mốc nhảy, bộ đếm vòng (`var_500`) và mốc chống kẹt. Bot gốc reset `pb25LastTileX/Y`, `pb25StuckSinceMs`, `var_500`.
2. **Mục 1, cách đánh số ải:** "ải 5 = 20062, ải 15 = 20067" là đúng theo thứ tự danh sách. Tuy vậy danh sách đi theo **cặp** `2006x` / `2011x`. Nếu thực tế mỗi lượt chỉ đi một map trong mỗi cặp, thì "Dừng sau ải N" của bạn sẽ lệch gấp đôi. Nên log thứ tự map thực tế của một lượt để xác nhận.
3. **Mục 11.6 (ải chuột chỉ đánh chuột):** vì bot gốc yêu cầu **sạch quái toàn map** mới sang ải, nên sau "hết chuột" nhớ quay lại đánh nốt quái thường trước khi xét sang ải.
4. **Mục 11.4, các khoảng chờ:** bot gốc dùng 1 giây ở điểm tuần, 2 giây "sạch", 0,8 giây chống kẹt. Có thể giảm "yên 3 giây" xuống khoảng 2 giây.

---

## E. Tóm tắt luồng một ải theo bot gốc (để đối chiếu)

```
mỗi frame:
  nếu đang ở map 20062 hoặc 20067:
      gửi 11163 [time, -1]; bật treo máy gốc; ép tự nhặt tiền, phạm vi 50 (gói 50581)
  nhặt đồ (§2#§)
  mục tiêu = quái gần nhất trên toàn map
  nếu có mục tiêu:
      (20062/20067 + bật nhảy) thử nhảy quanh quái -> nhảy được thì thôi
      xa >3 ô -> đi tới (chống kẹt 0,8s); gần -> skill 5 ô
  nếu không có quái sống toàn map & không còn đồ:
      giữ 2 giây; nếu cổng mở (10300 đúng map hoặc thấy cổng ở cuối vòng)
          -> "[PB25] Cổng đã mở + sạch quái + sạch đồ" -> sang ải
  nếu không: đi tuần 8 điểm, đứng 1s mỗi điểm
      cuối mỗi vòng: tìm cổng tới ải kế (bật cờ cổng mở); không có thì var_500++
      var_500 >= 5 -> sang ải (dự phòng)
```
