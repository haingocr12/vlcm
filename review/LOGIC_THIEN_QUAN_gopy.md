# Góp ý cho `LOGIC_THIEN_QUAN.md` (bản 01-10c)

Đối chiếu với logic Thiên Quan (`PhoBan_20`) của bot gốc trong `game.pak`. Mình đã đọc lại cả bản decompile lẫn **P-code** của hàm này, vì luồng điều khiển bị làm rối.

Ký hiệu:
- ✅ khớp bot gốc
- ⚠️ khác bot gốc, nên xem lại
- ❓ bot gốc không có, mình không kiểm chứng được

---

## A. Những chỗ nên sửa (ưu tiên cao)

### A1. Điều kiện sang tầng (mục 4, bước 9): nên bắt buộc "cổng mở"

Tài liệu của bạn cho sang tầng khi **(a) cổng mở HOẶC (b) đi hết một vòng tuyến không gặp quái**. ⚠️

Bot gốc làm khác:
- Bộ đếm "im lặng" (`pb20NoMonsterFrames`) **chỉ tăng khi `var_600 == true`**. Cờ này bật khi server gửi `10300` đúng cổng của tầng hiện tại với `isOpen = 1`. Bộ đếm cũng chỉ tăng khi không có mục tiêu.
- Đủ **30 frame** liên tiếp thì log "[PB20] Im lặng N frame -> qua tầng kế tiếp." và gọi `method_360()`. Hàm này đặt `ipb20 = chỉ số tầng hiện tại + 1`.
- SWF chạy 30 fps và vòng PB chạy theo `ENTER_FRAME`, nên 30 frame **≈ 1 giây**, không phải 3 giây.
- Bot gốc còn một đường dự phòng: bộ đếm `var_500 ≥ 4` cũng gọi `method_360()`. Mình chưa xác định chắc `var_500` đếm gì; nhiều khả năng là số lần tuần tra không tìm được mục tiêu.

**Hệ quả với tool của bạn:** điều kiện (b) đứng một mình có thể dẫn nhân vật tới cổng **chưa mở**. Nhân vật sẽ đứng chờ ở đó tới khi dính "kẹt tầng 90 giây" và cả lượt bị tính thất bại. Bạn đã tự nêu rủi ro này ở mục 10.1.

**Đề xuất sửa:**
```
Được sang tầng khi:
  (a) đã nhận 10300 với transId == map tầng hiện tại và isOpen == 1, VÀ
      không có quái mục tiêu trong ~1 giây liên tục;
  (b) [dự phòng] đã đi hết N vòng tuyến (bot gốc ~4) không thấy quái
      -> đi tới cổng; nếu sau X giây vẫn không nhận 10052 thì quay lại
      đi tuần, KHÔNG đứng chờ tới mốc kẹt 90 giây.
```

### A2. Lọc gói 10300 theo `transId` (mục 10.1)

Bot gốc **có** so `transId` với map hiện tại. Handler `§[7§` đọc `[int transId][int destPos][byte isOpen]` và chỉ bật `var_600` khi `transId == mapID hiện tại && isOpen == 1`. Gói 10300 của tầng khác bị bỏ qua.

→ Nên sửa đúng như vậy. Có thể bỏ qua `destPos`.

### A3. Qua cổng: bot gốc dùng gói tin, không bấm FAlert (mục 5)

Trình tự của bot gốc (đọc từ P-code) khi `ipb20` đã trỏ sang tầng kế:
1. Tìm cổng trong `sceneCharacters`: `type == 7` **và** `data.res.mapID == pb20Maps[ipb20]` (map tầng kế). Lấy tọa độ cổng.
2. Nếu khoảng cách tới cổng **> 1 ô**: `mainCharWalk("<map tầng kế>,-1,-1,0")` nếu chưa đang đi, rồi return. Phần này giống bạn.
3. Đã đứng tại cổng (≤ 1 ô):
   - Nếu nhân vật **lệch quá 2 ô so với ô (16,15)** (`|x−16| > 2` hoặc `|y−15| > 2`): gửi **10051** `[short 16][short 15]`.
   - Ngược lại: khi đã nhận **10052** (handler `§3=§` bật `pb20Received10052`), **hoặc** chưa gửi lần nào và đã chờ ≥ 3 giây, thì gửi **10053** `[short 16][short 15]`. Bot gốc **không** chờ hộp FAlert hiện ra rồi mới bấm.
4. Tọa độ `(16,15)` được viết cứng cho **mọi tầng**. Nhiều khả năng điểm qua ải ở 13 tầng nằm cùng một chỗ.

**Đề xuất:**
- Giữ cách bấm FAlert làm đường chính, vì nó qua đúng luồng UI và ít lộ hơn. Thêm **dự phòng**: nhận 10052 mà sau ~3 giây không thấy FAlert thì gửi 10053 `[16,15]`.
- Nhận diện cổng bằng `type == 7 && res.mapID == mapTầngKế`. Việc này cho bạn một điều kiện "đã tới cổng" rõ ràng, thay vì chỉ lặp `mainCharWalk` mỗi 4–8 giây.
- Ghi vào bảng dữ liệu cố định: "điểm qua ải các tầng ≈ (16,15)".

### A4. Gói vào phó bản 10701 (mục 3, `pbOpenStep` bước 4)

Bot gốc gửi `[int 1656][int 20038][byte 1][byte lvpb]`. Mảng lựa chọn cấp phó bản trong bot gốc là:

| lvpb | Nhãn trong bot gốc |
|---|---|
| 1 | Phó Bản sơ |
| 2 | Phó Bản Trung |
| 3 | Phó Bản cao (bot gốc **mặc định 3**) |

- Byte cuối `1` của bạn = **Phó Bản sơ**: ✅ "độ khó thấp nhất" là đúng. Nên cho người dùng chọn 1/2/3.
- Byte thứ 3: bot gốc **luôn gửi 1**. Mình **không tìm thấy** nhãn nào gắn với nó, nên chữ "thưởng thường" trong tài liệu của bạn là ❓ chưa kiểm chứng. Nên ghi "luôn = 1, chưa rõ ý nghĩa".
- Bot gốc gửi 10701 **sau 3 giây** kể từ lúc mở bảng (10711). Nếu bạn gửi ngay sau 10712 mà đôi khi bị từ chối, hãy thêm độ trễ này.
- Bot gốc đọc số lượt còn lại từ dữ liệu UI `uiInstance._instanceSelectPanel` (`totalCount − enterCount`, **id 2 = Thiên Quan**) chứ không tự giải 10712. Cả hai cách đều được. Nếu giải 10712 lỗi, cách đọc UI là đường dự phòng tốt.

### A5. Giữ mục tiêu thay vì chọn lại mỗi nhịp (mục 4, bước 8)

Tool của bạn chọn "quái gần nhất trong 20 ô" ở **mỗi nhịp 100 ms**. ⚠️ Cách này dễ đổi mục tiêu liên tục khi hai con ở gần nhau.

Bot gốc **khóa mục tiêu** (`pb20LockedTargetId`):
- Giữ nguyên tới khi con đó chết, hoặc cách nhân vật **> 12 ô**.
- Mất mục tiêu thì chọn **con gần nhất trên toàn map**, không giới hạn bán kính (`var_460 = 999999`).
- Mục tiêu cách > 3 ô thì `mainCharWalk` tới nó, rồi dùng skill.

→ Đề xuất thêm khóa mục tiêu, với ngưỡng nhả khoảng 12 ô.

→ Về mục 10.6 "Vượt ải phạm vi 99 ô": chính bot gốc cũng **không giới hạn bán kính** khi chọn mục tiêu mới. Nên làm tùy chọn này, vì nó cũng giảm nhu cầu đi tuần.

---

## B. Chỗ đã khớp bot gốc ✅

| Mục | Ghi chú |
|---|---|
| Map 20038 → 20050 | ✅ `pb20Maps` (trong mã có viết `334*60`, tức 20040) |
| NPC 1656 Tương Dương (58,67) | ✅ |
| Tuyến 17 điểm | ✅ trùng từng điểm với `pb20PosArrs` (mình dựng lại từ P-code) |
| Một tuyến cho 13 tầng (mục 1 lưu ý, 10.2) | ✅ Bot gốc **cũng chỉ có một tuyến** cho cả 13 tầng. Nhờ vậy điểm yếu 10.2 không tệ hơn bot gốc; hỗ trợ tuyến theo từng tầng trong XML là phần cộng thêm. |
| 10711 `[1656]` mở bảng | ✅ |
| `mainCharWalk("<map>,-1,-1,0")` để client tự tìm đường sang tầng | ✅ |
| 10726 → 10723 nhận thưởng | ✅ Bot gốc gửi 10723 **ngay** khi nhận 10726, sau **4 giây** mới chuyển sang chế độ ra. 1,5 + 5,5 giây của bạn hơi chậm hơn nhưng an toàn. |
| Rời phó bản đi cổng về 20002 | ✅ Bot gốc làm cụ thể hơn: tìm cổng `type == 7` có `res.mapID == 20002`, tới trong 5 ô rồi gửi **10051** `[x cổng][y cổng]`. Bạn có thể dùng làm dự phòng khi hộp xác nhận rời phó bản không hiện. |
| "Quá 4 phút chưa tới NPC" | ✅ Bot gốc dùng mốc 2 phút (đi lệch rồi tiếp cận lại) và 240 giây (làm lại từ đầu). |

---

## C. Phần bot gốc không có, mình không kiểm chứng được ❓

Các mục dưới đây là của riêng tool bạn. Mình không có dữ liệu trong `game.pak` để xác nhận đúng hay sai:

- Hoa hồng id 1201–1206, "mỗi ải tốn 1 hoa", kiểm hoa khi vào. Bot gốc **không kiểm hoa** ở đâu cả. Tuy vậy, danh sách nhặt mặc định của bot gốc có đủ 6 màu `[Hoa Hồng Đỏ/Vàng/Lam/Lục/Trắng/Đen]`, và có nhóm `[Hoa Hồng]` khớp regex `^Hoa\sHồng\s(Đỏ|Vàng|Lam|Lục|Trắng|Đen)$`. **Đề xuất:** trong Thiên Quan luôn nhặt hoa hồng như đã làm với đồng, đặc biệt khi bạn dùng hoa để qua ải và hồi sinh.
- Ải chuột (mục 6): bot gốc không có nhánh riêng cho "chuột" trong PB20.
- Hồi sinh (mục 8), gói 20075: không nằm trong phần mình đã phân tích.
- Trạng thái "đếm số" của boss (mục 10.5): bot gốc không có trong PB20. Để tham khảo: ở PB30, bot gốc nhận ra trạng thái đặc biệt của boss bằng **buff có `res.type == 26`** trong `data.buffInfo.buffArr`. Bạn có thể log `buffArr` của boss Thiên Quan để tìm type tương ứng.

---

## D. Góp ý nhỏ

1. **Mục 4, bước 9: "chờ 3 giây yên".** Bot gốc dùng khoảng 1 giây (30 frame) và chỉ khi cổng đã mở. Nên rút còn khoảng 1 giây sau khi có cổng mở, đúng như bạn định làm ở 10.3.
2. **Mục 10.8, kiểm hoa lúc vào.** Nếu đúng là mỗi ải tốn 1 hoa thì công thức nên là `hoa ≥ (13 − 1 − tầng_dừng_sớm) + số_hoa_hồi_sinh`, thay vì `max(...)`.
3. **Mục 10.7, hết hoa giữa chừng.** Nên kiểm **trước khi** đi tới cổng (lúc vừa có cổng mở), để khỏi đi tới cổng rồi mới bỏ.
4. **Mục 4, bước 5:** khi sang tầng, nhớ đặt lại cả **khóa mục tiêu** và bộ đếm im lặng (bot gốc reset `pb20LockedTargetId`, `pb20NoMonsterFrames`, `pb20GateSent`, `pb20Received10052`).
5. Nên ghi rõ trong tài liệu: **10052 = server → client**, **10053 = client → server**, cùng payload `[short 16][short 15]`.

---

## E. Tóm tắt luồng một tầng theo bot gốc (để đối chiếu)

```
mỗi frame (~33ms):
  nếu map hiện tại == pb20Maps[ipb20]:          # đang ở tầng cần đánh
      nhặt đồ (§2#§)
      mục tiêu = khóa cũ nếu còn sống & ≤12 ô, ngược lại quái gần nhất toàn map
      nếu có mục tiêu:
          xa >3 ô -> đi tới; gần -> skill 5 ô (dự phòng 10283/10083)
      nếu cổng mở (10300 đúng tầng) & không có mục tiêu:
          đếm frame; đủ 30 -> ipb20++ (sang chế độ qua cổng)
      không có mục tiêu & cổng chưa mở -> đi tuần 17 điểm
      đếm var_500 >= 4 -> ipb20++ (dự phòng)
  ngược lại (ipb20 đã trỏ sang tầng kế):
      tìm cổng type 7 có res.mapID == pb20Maps[ipb20]
      xa cổng >1 ô -> mainCharWalk("<tầng kế>,-1,-1,0")
      tại cổng: lệch (16,15) >2 ô -> gửi 10051 [16,15]
                ngược lại: nhận 10052 hoặc chờ ≥3s -> gửi 10053 [16,15]
```
