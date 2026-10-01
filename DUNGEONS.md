# Cách bot đi các phó bản: Liên Trảm, Thiên Quan, Doanh Trại, Phu Tử Trận, Mê Cung Trận

Nguồn: `Main.as` trong `game.pak` (đã giải chuỗi). Các mảng tọa độ lồng nhau bị bản decompile AS3 cắt mất, nên mình dựng lại từ **P-code**. Riêng `PhoBan_30` không decompile được (JPEXS báo timeout), nên toàn bộ phần PB30 được đọc từ P-code.

## 0. Tên phó bản ↔ hàm trong bot

| Chuỗi giao diện trong bot | Hàm chính | Map |
|---|---|---|
| **Phó Bản 15 (Liên Trảm)** | `PhoBan_15` | `20032` (1 map) |
| **Phó Bản 20 (Thiên Quan)** | `PhoBan_20` | `20038 … 20050` (13 tầng) |
| **Phó Bản 25 (Doanh Trại)** | `PhoBan_25` | `20060/20110 … 20069/20119` (20 map, xếp thành cặp) |
| **Phó Bản 30 (Phu Tử Trận)** | `PhoBan_30` | `20175` (1 map) |
| **Phó Bản 40 (Mê Cung Trận)** | `PhoBan_40` | `20033, 20177 … 20191` (16 phòng) + phòng thần bí `20192` |

Các hàm `PhoBan_15a`, `20a`, `25a`, `30a`, `30_goc`, `40_goc` **không được gọi ở đâu**. Nhiều khả năng đó là bản cũ còn sót lại (mã chết).

---

## 1. Chu trình chung (mọi phó bản)

### 1.1 Bộ điều phối: `method_420` (chạy mỗi frame, `ENTER_FRAME`)
- Mảng `var_3300[8]` đánh dấu phó bản đang đi. Thứ tự các ô: 15, 20, 25, 50, 60, 30, 40, 70.
- Khi đang ở trong một phó bản, bot nhận ra nó bằng `method_kiemtrapb(n)`: so `mapID` hiện tại với danh sách map, rồi gán chỉ số tầng `ipbXX`. Sau đó gọi `PhoBan_XX()` tương ứng.
- Khi đang ở **Tương Dương (20002)**, bot gọi `§-%§` để chọn phó bản kế tiếp và vào (mục 1.2).

### 1.2 Vào phó bản: `§-%§` (tại Tương Dương 20002)
1. Đi tới NPC **1656** quanh ô `(58, 67)` và khóa chọn NPC.
2. Gửi gói **10711** `[int 1656]` để mở bảng chọn phó bản.
3. Đọc `uiInstance._instanceSelectPanel`, lấy **số lượt còn lại** (`totalCount − enterCount`) của từng phó bản. id 1 → PB15, 2 → PB20, 3 → PB25… Một số phó bản còn tính theo thứ trong tuần (`Date.day`).
4. Chọn phó bản đầu tiên người dùng đã tích **và** còn lượt, bật cờ tương ứng trong `var_3300`.
5. Sau **3 giây**, gửi gói **10701**:
   `[int 1656][int mapID tầng đầu][byte 1][byte lvpb]`. `lvpb` mặc định là 3, chọn được trên giao diện.
6. Chống kẹt:
   - Quá 2 phút chưa vào được: log "[PB] quá 2 phút không vào được phó bản → rời vị trí, tiếp cận lại.", đi lệch khỏi NPC ±5–6 ô rồi quay lại.
   - Quá 240 giây: làm lại từ đầu.
7. Hết mọi phó bản:
   - log "`<tên>` :đã đi hết phó bản"
   - tùy chọn "Train lại khi đi phó bản xong" → `methon_02()`
   - tùy chọn "Tắt game khi đi phó bản xong" → `tatgame()`
   - chế độ "đi 3/2 lượt": log "đã hết 1 lượt Auto sẽ Login lại sau 2 phút. đi lượt 2."

### 1.3 Nhận thưởng và ra: dùng chung cho mọi phó bản
- Server gửi **10726** (hoàn thành) → handler `§]!§` gọi `§,@§`:
  - gửi **10723** `[]` để **nhận thưởng**
  - sau 4 giây bật `var_480` (chế độ "ra")
- Mỗi `PhoBan_XX` thấy `var_480` thì chuyển sang `var_450` và gọi `§8&§`:
  - tìm cổng dịch chuyển (`sceneChar.type == 7`) có `res.mapID == 20002`
  - đi tới trong phạm vi 5 ô
  - gửi **10051** `[short x][short y]` để **dùng cổng**, về Tương Dương. Vòng lặp chọn phó bản kế tiếp.

### 1.4 Cổng sang tầng (PB20, PB25): gói 10300
Server gửi **10300** `[int transId][int destPos][byte isOpen]`. Handler `§[7§` bật `var_600 = true` khi `transId` là map hiện tại và `isOpen == 1`. Khi đó log "[PB20] / [PB25] Cổng tầng kế tiếp ĐÃ MỞ tại map …".

### 1.5 Các thành phần dùng lại
- **Chọn quái:** duyệt `scene.sceneCharacters`, lấy `type == 2` (quái), chưa chết, `usable`, **gần nhất**.
- **Đánh:** khóa mục tiêu (`CHANGED_LOCKEDCHAR_BASE_ATTR`) → `train_useQuickbarFirst5Skill` (5 ô phím tắt) → nếu thất bại thì gửi gói ra chiêu thô 10283/10083 (xem `PACKETS_AND_ITEMS.md`). Một số phó bản còn phát `HIT_CHAR` để client tự đánh.
- **Nhặt đồ:** `type == 11` (vật rơi) → `method_190` / `§2#§` / `pb_tryPickupLikeTrain1` (xem `TRAIN_SKILL_PICKUP.md`).
- **Tuần tra:** mỗi phó bản có mảng điểm `pbXXPosArrs`. Không còn quái thì đi lần lượt tới điểm kế tiếp (con trỏ `var_3200`, hết mảng thì quay về 0).
- Cờ `§1B§`: nhánh đánh chỉ chạy khi cờ này bật. Cờ được đặt bởi checkbox trong tab "PB đội"; log ghi "Đã bật đánh phó bản" / "Đã bật không đánh phó bản".

---

## 2. Liên Trảm (PB15): `PhoBan_15`, map `20032`

**Điểm tuần tra** (`pb15PosArrs`, 14 điểm theo vòng):
`(40,95) (70,104) (102,106) (134,92) (148,71) (143,45) (146,19) (128,24) (93,15) (61,19) (30,31) (14,18) (17,51) (12,76)`
**Vị trí boss** `pb15BossPos = (78,56)`; bot đi qua điểm trung gian `(78,65)` trước khi tới đó.

Cách đi:
1. Tìm quái (`type == 2`) gần nhất **trong 20 ô** và khóa nó (`var_370`). Đánh dấu `check15Arr` và `pb15FoundMobThisRound = true`.
2. Quái xa hơn 3 ô thì `mainCharWalk` tới quái. Đủ gần thì dùng skill 5 ô; thất bại thì gửi `method_skill(10283)` (chiêu 53021 tại chỗ). Đồng thời phát `HIT_CHAR [mob, true, true]` để client tự đánh.
3. Có đồ rơi (`type == 11`) thì nhặt trước. Đồ cách hơn 2 ô thì đi tới, rồi `PICK_UP`; mỗi món cách nhau tối thiểu 1,2 giây.
4. Không còn quái quanh mình, hoặc 15 giây không có mục tiêu: sau 1,5 giây đi tới **điểm tuần tra kế tiếp**.
5. Đi hết một vòng 14 điểm mà không thấy quái: đi tới `(78,65)` rồi `(78,56)` để **đánh boss**.
6. Server báo hoàn thành (10726) → nhận thưởng → ra cổng (mục 1.3).

---

## 3. Thiên Quan (PB20): `PhoBan_20`, 13 tầng `20038 → 20050`

**Điểm tuần tra** (`pb20PosArrs`, 17 điểm):
`(87,65) (74,57) (56,42) (71,72) (43,70) (21,57) (7,58) (11,61) (22,34) (44,21) (56,14) (73,23) (90,23) (86,42) (98,54) (64,51) (49,38)`

Mỗi tầng:
1. **Khóa mục tiêu bền:** giữ `pb20LockedTargetId` cho tới khi quái chết hoặc cách hơn **12 ô**. Nhờ vậy bot không đổi mục tiêu liên tục. Hết mục tiêu thì chọn quái gần nhất mới.
2. Quái cách hơn 3 ô thì đi tới, rồi dùng skill 5 ô.
3. **Qua tầng**, theo một trong hai cách:
   - Server báo cổng mở (10300 → `var_600`), **hoặc**
   - Không có quái trong **30 frame** liên tiếp: log "[PB20] Im lặng N frame -> qua tầng kế tiếp.", rồi `method_360()` tăng `ipb20`.
4. **Dùng cổng sang tầng:**
   - Tìm cổng (`type == 7`) dẫn tới `pb20Maps[ipb20]`.
   - Nếu nhân vật ở gần ô `(16,15)` (lệch không quá 2 ô), gửi **10051** `[16,15]`.
   - Chờ server trả **10052**. Sau ít nhất 3 giây, gửi **10053** `[16,15]` để xác nhận chuyển tầng.
   - Nếu không, `mainCharWalk("<mapTầngKế>,-1,-1,0")` để client tự tìm đường sang map đó.
5. Tới tầng cuối và hoàn thành → nhận thưởng → ra (mục 1.3).

---

## 4. Doanh Trại (PB25): `PhoBan_25`, 20 map

`pb25Maps = [20060,20110, 20061,20111, … , 20069,20119]`: từng cặp map `2006x` / `2011x`.
**Điểm tuần tra** (`pb25PosArrs`, 8 điểm):
`(12,29) (21,25) (40,10) (42,47) (23,49) (52,26) (71,27) (81,16)`

Mỗi ải:
1. **Tuần tra có dừng:** đi tới từng điểm và **đứng lại khoảng 1 giây** (`pb25DwellStartMs`) trước khi sang điểm kế tiếp, để quái kịp xuất hiện hoặc lại gần.
2. Gặp quái thì đánh. Trước mỗi lượt đánh, gọi `pb25_tryJumpAroundMonster` (chỉ ở map 20062 và 20067, xem `PB25_JUMP.md`). Quái cách hơn 3 ô thì đi tới.
3. **Chống kẹt:** nếu đứng yên một ô quá 800 ms trong lúc đang đi (`pb25StuckSinceMs`) thì đi lại tới quái.
4. **Qua ải khi đủ cả 3 điều kiện:** cổng đã mở (10300, hoặc thấy cổng `type == 7` tới `pb25Maps[ipb25+1]`), **sạch quái** và **sạch đồ**. Khi đó log "[PB25] Cổng đã mở + sạch quái + sạch đồ -> qua ải kế tiếp." và gọi `method_360()`. Ngoài ra, nếu 5 lần liên tiếp không thấy mục tiêu cũng qua ải.
5. Đi sang ải: `mainCharWalk("<pb25Maps[ipb25]>,-1,-1,0")`.
6. **Map 20062 và 20067** (hai ải có nhảy), xử lý ở mỗi frame:
   - gửi **11163** `[double getTimer()][int -1]` (nhặt túi đồ; bagID −1, có lẽ là "tất cả")
   - bật auto treo máy gốc của game theo cài đặt (`method_89`), ép `afkInfo2`: `isAutoPickUpMoney = 1`, `afkRangeCount = 50`
   - lưu cấu hình auto lên server bằng `method_197` → gói **50581**

   Bot gốc còn xử lý gói rơi đồ **11162** `[monsterID, bagID, x, y, goodsID, amount, quality]` bằng cách gửi ngay **11163** `[time, bagID]`, tức nhặt túi từ xa.
   Nhiều khả năng 20062/20067 là các ải rơi đồng (ải chuột); điều này chưa kiểm chứng.

---

## 5. Phu Tử Trận (PB30): `PhoBan_30`, map `20175`

Đây là phó bản **đánh quái theo thứ tự NPC gợi ý**. Tất cả đọc từ P-code.

**Điểm** (`pb30PosArrs`, 8 điểm): `(57,27) (47,22) (36,15) (28,22) (23,31) (35,36) (45,31) (33,25)`

1. **Chuẩn bị** (tránh đánh nhầm con khác thứ tự):
   - Lưu id thú chiến đang dùng: log "[PB30] Lưu thú chiến id=…".
   - Nếu ám khí đang bật (`anqiInfo.isOpen == 1`), gửi **52005** `[byte 0]`: log "[PB30] Đã gửi tắt ám khí".
   - Gửi **53043** `[byte 0]`: log "[PB30] Đã gửi tắt cung".
   - Gửi **50039** `[int thúId]`: log "[PB30] Gửi cất thú chiến id=…". Chỉ gửi tối đa 1 lần mỗi 3 giây.
2. **Hỏi thứ tự:**
   - Đi tới NPC **1738** tại `(12,14)`, trong phạm vi 3 ô.
   - Gửi **10129** `[int 1738]`, tối đa 1 lần mỗi 2 giây: log "[PB30] hỏi NPC lấy thứ tự."
3. **Đọc gợi ý:** lấy `GameInstance.npcTishiDict[1738]` (dòng gợi ý hiện trên đầu NPC) rồi làm sạch:
   - bỏ thẻ HTML `<…>`, bỏ `[…]` và `【…】`
   - bỏ cụm "gợi ý lượt này"
   - thay dấu phân cách `, ， : ： ; ； | / \`, tab và xuống dòng bằng khoảng trắng
   - chuyển về chữ thường, tách theo khoảng trắng

   Kết quả là mảng tên `pb30Order`: log "[pb30] Thứ tự: …".
4. Đi xuống khu quái `(36,25)`: log "[PB30] Đã tới khu quái."
5. **Đánh theo thứ tự:**
   - Với `pb30Order[pb30OrderIndex]`, tìm quái có tên khớp. Tên quái lấy theo thứ tự ưu tiên `getHeadFaceNickName()` → `headFace.nickName` → `data.name` → `data.res.name`, và được làm sạch giống bước 3.
   - Chọn con gần nhất khớp tên. Cách hơn 4 ô thì đi tới. Không thấy thì tuần tra theo `pb30PosArrs`.
   - Ra chiêu cách nhau ≥ 300 ms. Skill theo phái: 53021 hoặc 51012/51022/51032/51042/51052/51062 (xem `PACKETS_AND_ITEMS.md`).
   - Con đó chết thì `pb30OrderIndex++`.
   - Đánh hết mảng: log "[PB30] Đã đánh hết đợt, quay lại NPC." rồi xóa `pb30Order` và quay lại bước 2 để hỏi đợt tiếp theo.
6. **Boss "Khôi Khôi"** (so khớp tên "khôi" / "khôi khôi"):
   - Nếu đang cưỡi ngựa thì xuống (`UP_DOWN_HORSE`).
   - Gửi **50037** `[int thúId]` để **thả lại thú chiến**: log "[PB30] Đã gửi thả thú chiến id=…".
   - Khóa mục tiêu, phát `HIT_CHAR`, rồi ra chiêu.
   - Nếu bật " Nhảy khi đánh Boss Khôi Khôi" (`chk_jumpBossKhoi`): gọi `pb30_tryJumpAroundMonster`. Hàm này dùng chung thuật toán 12 hướng với PB25, nhưng **chỉ nhảy khi boss đang mang buff có `res.type == 26`** (hàm `§`5§`), tức nhảy né lúc boss vào trạng thái đặc biệt.

---

## 6. Mê Cung Trận (PB40): `PhoBan_40`, 16 phòng

`pb40Maps = [20033, 20177, 20178, …, 20191]`; phòng thần bí `pb40thanbi = 20192`.
**Hai cửa trong mỗi phòng** (`pb40PosArrs`): **cửa trái L = (40,26)**, **cửa phải R = (116,28)**.
Trong vòng lặp PB40 **không có code đánh quái**. Đây thuần túy là **giải mê cung bằng cách thử cửa**.

Thuật toán, dùng mảng nhớ `pb40Door[15]` với giá trị `"no" / "L" / "R"` cho từng phòng:
1. Vào phòng `i`:
   - `pb40Door[i] == "L"` → đi cửa trái `(40,26)`
   - `"R"` → cửa phải `(116,28)`
   - `"no"` (chưa thử) → gán thử một cửa
2. Tới trong phạm vi 5 ô của cửa: ghi lại cửa đã chọn (`x == 40 ? "L" : "R"`), gửi **10051** `[short x][short y]` để dịch chuyển, và lưu `pb40prev = phòng hiện tại`.
3. Sang được phòng mới, bot kiểm tra kết quả:
   - Nếu phòng mới là **phòng kế tiếp** (`index(prev) + 1`, hoặc từ 20033 sang 20177), cửa vừa chọn là **đúng**, giữ nguyên.
   - Nếu bị đưa về phòng khác (sai cửa), hoặc quá 4 giây vẫn ở phòng cũ, thì **đổi cửa** của phòng trước: `L ↔ R`.
4. Ngoài hai cửa chuẩn, nếu thấy cổng (`type == 7`) thì cũng đi tới và dùng bằng 10051.
5. **Phòng cuối 20191:**
   - Reset `pb40Door`.
   - Đi tới `(84,64)`, khóa NPC **1740**.
   - Gửi **52059** `[]`, mở chức năng NPC (`NPC_REQ_FUNCTION 24`), đóng hộp thoại.
   - Bật `pb40npcGift = true` (đã nhận quà NPC).
   - Sau đó ra như mục 1.3.
6. Phòng thần bí `20192` vẫn được `method_kiemtrapb(40)` tính là "đang trong PB40", để không bị coi là đã thoát.

Vì mảng `pb40Door` được giữ suốt lượt, sau một lần đi bot **nhớ đường đúng**. Thêm nữa, mỗi phòng chỉ có 2 lựa chọn, nên nếu đoán sai thì chỉ cần một lần bị đẩy lại là tìm ra cửa đúng.

---

## 7. Bảng gói tin dùng trong 5 phó bản

| Cmd | Chiều | Dùng ở | Nội dung |
|---|---|---|---|
| 10711 | gửi | vào PB | `int 1656` (mở bảng phó bản ở NPC) |
| 10701 | gửi | vào PB | `int 1656, int mapTầngĐầu, byte 1, byte lvpb` |
| 10051 | gửi | ra PB, PB20, PB40 | `short x, short y`: dùng cổng / điểm dịch chuyển |
| 10052 | nhận | PB20 | server xác nhận yêu cầu chuyển tầng |
| 10053 | gửi | PB20 | `short 16, short 15`: xác nhận chuyển tầng |
| 10300 | nhận | PB20, PB25 | `int transId, int destPos, byte isOpen`: cổng tầng mở |
| 10726 | nhận | tất cả | phó bản hoàn thành |
| 10723 | gửi | tất cả | nhận thưởng |
| 11162 | nhận | PB25 | rơi đồ: `int monsterID, int bagID, short x, short y, int goodsID, int amount, byte quality` |
| 11163 | gửi | PB25 map 20062/20067 | `double time, int bagID` (−1 ở 2 map này): nhặt túi |
| 50581 | gửi | PB25 | lưu cấu hình auto treo máy |
| 10129 | gửi | PB30 | `int 1738`: hỏi thứ tự |
| 52005 / 53043 | gửi | PB30 | `byte 0`: tắt ám khí / tắt cung |
| 50039 / 50037 | gửi | PB30 | `int thúId`: cất / thả thú chiến |
| 52059 | gửi | PB40 | quà NPC 1740 phòng cuối |
| 10083 / 10283 | gửi | PB15/20/25/30 | ra chiêu thô (dự phòng) |
