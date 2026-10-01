# Logic phó bản Thiên Quan (bản 01-10c)

Tài liệu mô tả đúng những gì code hiện tại đang làm (VlcmTrain.as + vlcmpanel.cpp), để bạn kiểm tra.
Mỗi phần có tên hàm trong code để bạn đối chiếu. Cuối file có danh sách các điểm mình thấy còn yếu hoặc chưa chắc.

---

## 1. Dữ liệu cố định

| Mục | Giá trị |
|---|---|
| Khóa phó bản | `tq` |
| Map các tầng | 20038 → 20050 (13 tầng, theo thứ tự) |
| NPC vào phó bản | 1656 ở Tương Dương (map 20002, ô 58,67) |
| Tuyến đi tuần mặc định | 17 điểm: (87,65) (74,57) (56,42) (71,72) (43,70) (21,57) (7,58) (11,61) (22,34) (44,21) (56,14) (73,23) (90,23) (86,42) (98,54) (64,51) (49,38) |
| Tuyến từ file | `data\phoban\ThienQuan.xml` (có file thì thay tuyến mặc định) |
| Bán kính tìm quái / túi đồ | 20 ô quanh nhân vật (`PB_R`) |
| "Yên" | 3 giây không có quái trong 20 ô (`PB_QUIET`) |
| Kẹt tầng | 90 giây không tiến triển → thoát, tính thất bại (`PB_FLOOR_STUCK`) |
| Tối đa cả lượt | 25 phút → thoát, thất bại (`PB_RUN_MAX`) |
| Hoa hồng | id 1201–1206, cộng cả 6 loại (`roses()`) |

**Lưu ý:** 13 tầng dùng chung **một** tuyến. Khi sang tầng mới, tool đi lại từ điểm đầu của tuyến.

---

## 2. Panel quyết định có chạy Thiên Quan không (`PbReady`, `PbWanted`, `PbArgs`)

Thiên Quan chỉ được đưa vào danh sách gửi xuống game khi đủ tất cả các điều kiện sau:

1. Ô `[PB] Thiên Quan` đang tick.
2. Chưa "xong" hôm nay. Xong nghĩa là đủ số lượt/ngày, hoặc đã bị bỏ qua (hết lượt game / thiếu hoa / không vào được).
3. Nếu tick "Làm từ … đến …": giờ máy nằm trong khung. Khung được phép qua nửa đêm.
4. Nếu tick "Làm khi liên trảm từ A và còn từ B phút": buff liên trảm ≥ A **và** còn ≥ B phút.
5. Không bị SWF báo "pb wait" trong 60 giây gần nhất.

Khi chưa đủ điều kiện: ô trạng thái hiện "Đang chờ" và dòng "Hôm nay" ghi lý do. Trong lúc đó Đánh quái vẫn chạy.

Lệnh gửi xuống: `pb_start list=…,tq,… tq_runs= tq_rev= tq_minr= [tq_lz=ge:A:B] [tq_sf=N] [tq_nomob=N] [tq_route=x:y;…] done=…`

---

## 3. Ở thành → vào phó bản

**`pbNextKey`** — chọn phó bản kế tiếp trong danh sách. Thiên Quan bị bỏ qua nếu:
- đã bị đánh dấu skip;
- đã đủ số lượt;
- điều kiện liên trảm không đạt (lúc này báo `pb wait tq <lý do>`).

**`pbTownStep`**
- Không ở Tương Dương thì về Tương Dương. Dịch chuyển chỉ dùng khi VIP; không VIP thì đi bộ.
- Đi tới NPC 1656: lặp lệnh đi mỗi 5 giây.
- Quá 4 phút chưa tới thì log cảnh báo, nhưng vẫn tiếp tục thử.

**`pbOpenStep`**
1. Gửi `send_10711(1656)` để mở bảng phó bản, chờ gói 10712. Thử tối đa 4 lần, mỗi lần cách 4 giây; vẫn không được thì quay lại bước đi tới NPC.
2. Đọc từ 10712 số lượt đã vào / tổng lượt của Thiên Quan. Hết lượt → skip "hết lượt hôm nay".
3. Kiểm tra hoa: cần **hoa ≥ max(tq_minr, tq_rev)**. Thiếu → skip "thiếu hoa hồng (có/cần)".
4. Gửi `send_10701([1656, 20038, 1, 1])`: thưởng thường, độ khó thấp nhất.

**`pbEnterStep`**
- Chờ vào map 20038. Quá 20 giây chưa vào thì thử lại.
- Thất bại 3 lần → skip "không vào được".

---

## 4. Trong phó bản — mỗi nhịp 100ms (`pbInStep`, phần chung TQ/DT)

Các bước kiểm tra theo thứ tự, gặp điều kiện nào thì xử lý và dừng nhịp đó:

1. **Không còn ở map Thiên Quan.**
   - Ở Tương Dương mà đã nhận 10726 → kết thúc "hoàn thành".
   - Ở Tương Dương mà chưa nhận 10726 → "bị đưa ra khỏi phó bản", thất bại.
2. **Đã nhận 10726** (hoàn thành) → sang bước nhận thưởng (mục 7).
3. **Quá 25 phút** → thoát, thất bại.
4. **Rời khi không có quái** (nếu tick): N phút liền không thấy con quái sống nào trên toàn map → thoát, thất bại.
5. **Vừa sang tầng mới.** Tool đặt lại trạng thái tầng:
   - điểm tuần về đầu tuyến;
   - xóa cờ "cổng mở" (10300);
   - xóa cờ "đi hết vòng không thấy quái";
   - log `pb floor tq k/13`.
   - Nếu tick "Dừng sau khi vượt qua ải N" và chỉ số tầng ≥ N → thoát, **tính hoàn thành**. Ví dụ N = 3: tới tầng thứ 4 thì ra.
6. **Kẹt tầng:** 90 giây ở tầng này mà không tiến triển → thoát, thất bại.
7. **Ải chuột:** thấy quái tên có chữ "chuột" → chế độ ải chuột (mục 6).
8. **Đánh:**
   - Tâm đánh = vị trí nhân vật, bán kính 20.
   - `support`: hồi máu / buff theo tab Kỹ năng.
   - `pickStep`: nhặt đồ theo luật nhặt. Trong phó bản, đồng luôn được nhặt.
   - Chọn quái gần nhất trong 20 ô. Đánh mọi loại quái, kể cả boss.
   - Có quái → đánh, đánh dấu "vòng này có thấy quái", cập nhật tiến triển.
9. **Không có quái:** chờ 3 giây yên. Sau đó:
   - **Được sang tầng** nếu một trong hai điều kiện đúng:
     - (a) server đã báo cổng mở (10300, byte isOpen = 1) kể từ khi vào tầng này;
     - (b) đã đi hết một vòng tuyến mà không gặp con quái nào.
     
     Còn túi đồ cần nhặt thì nhặt trước. Sau đó gọi `mainCharWalk("<map tầng kế>,-1,-1,0")`: game tự tìm cổng. Lệnh đi lặp lại mỗi 4 giây, hoặc 8 giây nếu đang đi.
   - **Chưa được** → đi tuần (`pbPatrol`):
     - đi tới điểm tuần hiện tại, lặp lệnh mỗi 1,5 giây;
     - tới điểm (≤ 2 ô) thì sang điểm kế;
     - hết một vòng mà không gặp quái → bật cờ (b).

---

## 5. Câu hỏi qua ải và rời phó bản (`pbConfirmDialogs`)

- Bước lên cổng thì server gửi **10052** `[x, y, câu hỏi]`, game hiện hộp FAlert. Tool ghi lại trong 15 giây.
- Thấy FAlert có nút `FAlert.ok`:
  - **Thiên Quan mà hết hoa (< 1)** → không bấm, thoát phó bản "hết hoa hồng".
  - Còn hoa → bấm Xác nhận (MouseEvent.CLICK), log `pb confirm qua ải: …`.
- Lúc đang đi ra cổng ra (trong 20 giây), hộp hỏi rời phó bản cũng được bấm Xác nhận.

---

## 6. Ải chuột (nếu gặp trong Thiên Quan) (`pbMouseStep`)

- Nhận ra ải chuột theo tên quái có chữ "chuột". Bán kính đánh 200.
- Đánh từng con, con gần nhất trước.
- Chuột chết → chờ gói rơi đồ của đúng con đó (tối đa 1,5 giây) → nhặt hết → mới đánh con kế.
- Không thấy chuột: đi tuần 1 vòng, yên 3 giây → "hết chuột và đồng" → quay về đánh bình thường.

---

## 7. Hoàn thành → nhận thưởng → ra (`pbRewardStep`, `pbExitStep`)

1. Nhận **10726** → chờ 1,5 giây → `send_10723()` (nhận thưởng), log `pb reward tq`.
2. Sau 5,5 giây: nhặt nốt đồ quanh đó, đóng bảng phó bản.
3. Đi cổng ra: `mainCharWalk("20002,-1,-1,0")`, lặp mỗi 6 giây; hộp hỏi rời phó bản được xác nhận như mục 5.
4. Về tới Tương Dương → `pb end tq ok …`. Panel cộng 1 lượt "hôm nay".
5. Còn lượt thì vòng lại mục 3; hết thì sang phó bản kế hoặc `pb finish`.

---

## 8. Chết trong phó bản (`pbStep`, `pbRevive`)

- Chết → đếm số lần chết của lượt này.
- Thử hồi sinh: lần đầu sau 2 giây, các lần sau cách nhau thêm 8 giây, tối đa 6 lần.
- **Hồi tại chỗ** (tốn hoa) nếu số lần chết ≤ "Số hoa hồi sinh" **và** còn hoa. Ngược lại **về thành**.
- Cách bấm: bấm nút trên bảng hồi sinh của game (ReLivePanel). Không thấy bảng thì gửi 20075 như nút của bảng (0 = tại chỗ, 1 = về thành).
- Sau khi sống lại:
  - còn ở map Thiên Quan → đánh tiếp tầng đó;
  - bị về thành → lượt thất bại "chết N lần, đã về thành".

---

## 9. Mất kết nối / mở lại client

- Panel tự mở lại client khi tắt bất thường, không giới hạn số lần.
- Vào game mà nhân vật đang đứng trong map Thiên Quan (và Thiên Quan đang tick) → chạy tiếp lượt dở trước tiên.

---

## 10. Các điểm còn yếu / chưa chắc (để bạn xem)

1. **Cổng mở (10300):** tool nhận bất kỳ 10300 có isOpen = 1 sau khi vào tầng, không so `transId` với cổng của tầng đang đứng. Nếu server gửi 10300 cho cổng khác, tool có thể đi tìm cổng sớm. Lúc đó game tự tìm đường; nếu cổng chưa mở thì nhân vật đứng ở cổng cho tới khi kẹt 90 giây.
2. **Một tuyến cho 13 tầng:** nếu bố cục các tầng khác nhau, nhiều điểm có thể nằm ngoài vùng đi được. Khi đó tool vẫn đi hết vòng rồi mới sang tầng (điều kiện b), nên chậm.
3. **Chờ yên 3 giây** trước khi đi tuần / sang tầng (Liên Trảm đã bỏ, Thiên Quan vẫn giữ).
4. **Lặp lệnh đi tuần 1,5 giây** và **lặp lệnh đi sang tầng 4 giây** — có thể rút ngắn như Liên Trảm.
5. **Boss Thiên Quan:** đánh như quái thường. Tool chưa có tùy chọn "Bỏ qua đánh Boss ở trạng thái đếm số" như tool kia, vì mình chưa biết trạng thái đó trong game là gì.
6. **"Vượt ải với phạm vi 99 ô"** (tool kia có): chưa làm. Hiện bán kính tìm quái cố định 20 ô.
7. **Hết hoa giữa chừng:** chỉ kiểm lúc bấm Xác nhận qua ải. Tool không kiểm trước khi bước lên cổng.
8. **Kiểm hoa lúc vào:** hoa ≥ max(tối thiểu, số lần hồi sinh). Không tính số hoa cần để qua 12 ải (mỗi ải tốn 1 hoa) cộng thêm số hoa hồi sinh.
9. Chưa chạy trên game thật: câu hỏi 10052, bảng hồi sinh, cổng ra.
