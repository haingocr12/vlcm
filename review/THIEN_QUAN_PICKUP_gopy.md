# Góp ý: logic nhặt đồ trong Thiên Quan

Đối chiếu phần nhặt đồ trong `LOGIC_THIEN_QUAN.md` (mục 1, 4.8, 4.9, 7) với cách bot gốc nhặt đồ trong `PhoBan_20`.

## 1. Bot gốc nhặt đồ trong Thiên Quan như thế nào

`PhoBan_20` gọi hàm `§2#§()` ở **đầu mỗi frame** khi đang ở tầng cần đánh. Nếu hàm trả về `true` (còn đồ đang nhặt) thì bot **return luôn**, không chọn quái, không đánh, không đếm "im lặng".

### 1.1 Chọn túi đồ: `§'=§()`
- Chế độ `var_98 == 1` ("Không nhặt") → không nhặt gì.
- Duyệt `scene.sceneCharacters` và chỉ lấy đối tượng có `type == BAG`, `usable`, không phải nhân vật mình.
- Bỏ qua id đang nằm trong danh sách cấm `§;A§`. Giá trị trong danh sách có hai dạng:
  - `true`: cấm vĩnh viễn trong phiên.
  - mốc thời gian: cấm tới mốc đó; hết hạn thì xóa khỏi danh sách.
- Khoảng cách tính bằng **Manhattan** `|dx| + |dy|` từ **vị trí nhân vật**. **Không có giới hạn bán kính**, quét cả map.
- Chế độ "Chỉ nhặt các vật phẩm sau" (`var_98 == 2`): lấy món khớp tên đứng **trước** trong danh sách; nếu cùng thứ tự thì lấy món **gần hơn**. Cách so tên giống `method_170`: không phân biệt hoa thường, khớp chuỗi con, và có các nhóm `[Hoa Hồng]`, `[Đồng]`, `[Mảnh Bí Kíp]`…
- Chế độ "nhặt tất cả" (`var_98 == 3`): lấy món gần nhất.

### 1.2 Đi tới và nhặt: `§2#§()`
- Mỗi món có mốc "bắt đầu nhắm tới" (`§;,§[id]`).
- Gửi lệnh nhặt qua luồng của client: `PipeManager.sendMsg(PICK_UP, id)`. Hai lệnh nhặt cách nhau ít nhất **200 ms** (`§&?§`).
- Nếu chưa nhắm món này (`§%&§ != id`): `clear()` đường đi cũ rồi đi tới ô của món (`method_16`), ghi lại `§%&§ = id`.
- **Chống kẹt:** nhắm cùng một món quá **6 giây** mà chưa nhặt được thì:
  - cấm món đó **10 giây** (`§;A§[id] = now + 10000`)
  - bước lệch 1 ô (`x + 1`) để gỡ kẹt
  - log "[PB] Vật phẩm … kẹt > 6s -> bỏ qua tạm 10s."
- Còn món cần nhặt thì trả `true`, khiến cả vòng đánh và bộ đếm qua tầng phải chờ.

### 1.3 Hệ quả quan trọng
- Bộ đếm "im lặng 30 frame" để qua tầng nằm **sau** lệnh gọi `§2#§`. Vì vậy **còn đồ là chưa qua tầng**: bot gốc ngầm yêu cầu "sạch đồ" mà không cần điều kiện riêng.
- Khi đang đi sang tầng kế (chế độ qua cổng), bot gốc **không nhặt** nữa.
- Sau khi nhận thưởng (10726/10723), bot gốc **không nhặt nốt** mà đi ra luôn sau 4 giây.

### 1.4 Điểm yếu của chính bot gốc (bạn nên làm tốt hơn)
- Danh sách cấm **vĩnh viễn** khi server báo **"Ngươi không thể nhặt vật phẩm này"** lấy id từ `var_13["pickup_goods"]`. Biến này là món đang nhặt của **vòng train thường**, không phải của phó bản (`§%&§`). Trong Thiên Quan, thông báo này có thể cấm nhầm món hoặc không cấm gì.
- Vì vậy, một món không bao giờ nhặt được (đồ của người khác, túi đầy…) sẽ bị thử theo chu kỳ "6 giây kẹt → nghỉ 10 giây → thử lại" **mãi mãi**. Trong lúc đó bộ đếm qua tầng không chạy, nên tầng có thể kẹt vô hạn.

---

## 2. Góp ý cho tool của bạn

### 2.1 Bán kính nhặt 20 ô (mục 1: `PB_R`) ⚠️
Bot gốc nhặt **toàn map**. Với 20 ô quanh nhân vật, đồ rơi ở góc xa (quái chết khi nhân vật đang đứng ở điểm tuần khác) sẽ bị bỏ lại. Hoa hồng và đồng là thứ bạn cần cho Thiên Quan.
→ Nên tách **bán kính nhặt** khỏi bán kính tìm quái, hoặc cho nhặt toàn map (sắp theo khoảng cách) **khi không có quái**.

### 2.2 Món không nhặt được làm kẹt tầng ⚠️ (quan trọng nhất)
Mục 4.9 của bạn: "Còn túi đồ cần nhặt thì nhặt trước, sau đó mới đi sang tầng". Nếu có một túi **không nhặt được**, tool sẽ đứng đó tới mốc "kẹt tầng 90 giây" và cả lượt bị tính thất bại.
→ Đề xuất, làm tốt hơn bot gốc:
1. Mỗi túi có **số lần thử** và **mốc bắt đầu**. Không nhặt được sau khoảng 5–6 giây thì cấm tạm khoảng 10 giây và bước lệch 1 ô, giống bot gốc.
2. Bị cấm tạm **2–3 lần** thì **cấm hẳn** trong lượt.
3. Khi nhận thông báo hệ thống "Ngươi không thể nhặt vật phẩm này", **cấm hẳn đúng id túi đang nhắm** (biến mục tiêu nhặt của chính phó bản, không dùng biến của train).
4. Mọi điều kiện "còn đồ cần nhặt" (trước khi sang tầng, "yên 3 giây", "hết chuột và đồng") phải **bỏ qua các túi đã bị cấm**. Nếu không, tool chờ vô hạn.

### 2.3 Thứ tự nhặt và đánh (mục 4.8)
Bot gốc **luôn nhặt trước, đánh sau**: còn đồ là không đánh. Tool của bạn gọi `pickStep` trước khi chọn quái, nhưng tài liệu không nói rõ nhịp đó có đánh luôn không.
→ Nên ghi rõ một trong hai cách:
- **Như bot gốc:** còn đồ thì không đánh. Đơn giản, nhưng có thể bị quái đánh lúc đi nhặt.
- **Tốt hơn:** có quái ở gần (≤ 3–5 ô) thì đánh trước; hết quái gần thì nhặt. Nhờ vậy nhân vật không chạy đi nhặt đồ khi đang bị đánh.

### 2.4 Tần suất lệnh nhặt và lệnh đi
- Gửi `PICK_UP` cách nhau **≥ 200 ms**. Nhịp 100 ms của bạn có thể gửi gấp đôi bot gốc.
- Chỉ gửi lại `mainCharWalk` tới túi khi **đổi mục tiêu**, hoặc khi nhân vật đã dừng đi mà chưa tới. Bot gốc cũng chỉ đi khi `getStatus() != WALK` hoặc đổi id.
- Bot gốc chỉ đi tới khi khoảng cách Manhattan > 1. Ở ≤ 1 ô thì chỉ gửi `PICK_UP`.

### 2.5 Hoa hồng và đồng
- Danh sách nhặt mặc định của bot gốc có `[Hoa Hồng Đỏ … Đen]`. Bot gốc còn có nhóm `[Hoa Hồng]` (regex `^Hoa\sHồng\s(Đỏ|Vàng|Lam|Lục|Trắng|Đen)$`) và `[Đồng]` (regex `^\d+\sđồng$`).
- Bạn đã luôn nhặt đồng trong phó bản. Nên **luôn nhặt cả hoa hồng** trong Thiên Quan, bất kể luật nhặt người dùng đặt, vì tool dùng hoa để qua ải và hồi sinh (mục 5, 8).
- Nên **ưu tiên hoa hồng trước** các món khác khi có nhiều túi cùng lúc.

### 2.6 Túi đầy
Bot gốc không kiểm túi đầy. Nó chỉ dựa vào thông báo "không thể nhặt", mà thông báo này lại bị gắn nhầm biến (xem 1.4).
→ Nên kiểm số ô trống trước khi đi nhặt. Túi đầy thì **chỉ nhặt đồng và hoa hồng** (đồng không chiếm ô; hoa hồng có thể cộng dồn), bỏ các món khác, để tránh kẹt tầng.

### 2.7 Nhặt nốt sau khi nhận thưởng (mục 7.2)
Bot gốc không làm bước này. Cách của bạn (chờ 5,5 giây rồi nhặt nốt) là tốt hơn. Nên giới hạn **tổng thời gian nhặt nốt** (khoảng 10–15 giây) và bỏ qua túi bị cấm, để không bị kẹt khi ra.

### 2.8 Nhặt từ xa bằng gói 11163 (chỉ để biết)
Bot gốc có hàm xử lý gói rơi đồ **11162** `[monsterID, bagID, x, y, goodsID, amount, quality]`. Hàm này gửi ngay **11163** `[double time][int bagID]` để nhặt **không cần đi tới**. Ở Doanh Trại map 20062/20067, bot gốc còn gửi `11163 [time, -1]` mỗi frame.
→ Có thể dùng làm **dự phòng** khi đi tới mà không nhặt được. Tuy vậy, cách này **dễ bị server phát hiện** nếu server kiểm khoảng cách. Không nên dùng làm cách chính.

---

## 3. Đoạn đề xuất thay cho mục 4.8 / 4.9 trong `LOGIC_THIEN_QUAN.md`

```
Nhặt đồ (mỗi nhịp, trước khi xét sang tầng):
  - Ứng viên: túi type BAG, usable, không bị cấm, khớp luật nhặt
    (luôn gồm đồng + hoa hồng), sắp ưu tiên: hoa hồng > đồng > theo
    thứ tự luật > gần hơn. Phạm vi: toàn map khi không có quái,
    ≤ PB_R khi đang có quái.
  - Có quái trong ≤ 4 ô -> đánh trước, chưa nhặt.
  - Tới túi: Manhattan > 1 -> đi tới (chỉ gửi lại khi đổi túi / đã dừng);
    ≤ 1 -> PICK_UP, cách nhau ≥ 200 ms.
  - Nhắm một túi > 6 s chưa được -> cấm tạm 10 s, bước lệch 1 ô;
    cấm tạm lần thứ 3 -> cấm hẳn trong lượt.
  - Server báo "Ngươi không thể nhặt vật phẩm này" -> cấm hẳn túi đang nhắm.
  - Túi đầy -> chỉ còn nhặt đồng + hoa hồng.
  - "Còn đồ cần nhặt" chỉ tính túi CHƯA bị cấm.
```
