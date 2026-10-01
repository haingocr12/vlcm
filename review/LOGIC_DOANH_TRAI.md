# Logic phó bản Doanh Trại (bản 01-10c)

File này mô tả đúng những gì code hiện tại đang làm (VlcmTrain.as và vlcmpanel.cpp). Mỗi phần có ghi tên hàm để bạn đối chiếu với code. Các điểm mình thấy còn yếu hoặc chưa chắc nằm ở cuối file.

---

## 1. Dữ liệu cố định

| Mục | Giá trị |
|---|---|
| Khóa phó bản | `dt` |
| Map các ải (theo thứ tự) | 20060, 20110, 20061, 20111, 20062, 20112, 20063, 20113, 20064, 20114, 20065, 20115, 20066, 20116, 20067, 20117, 20068, 20118, 20069, 20119 (20 ải) |
| NPC vào phó bản | 1656 ở Tương Dương (map 20002, ô 58,67) |
| Tuyến đi tuần mặc định | 8 điểm: (12,29) (21,25) (40,10) (42,47) (23,49) (52,26) (71,27) (81,16) |
| Tuyến lấy từ file | `data\phoban\DoanhTrai.xml`. Có file thì dùng tuyến trong file thay cho tuyến mặc định. |
| Ải được nhảy | 20062 và 20067 (ải 5 và ải 15 theo thứ tự trên) |
| Bán kính tìm quái / túi đồ | 20 ô quanh nhân vật (`PB_R`) |
| "Yên" | 3 giây không có quái trong 20 ô (`PB_QUIET`) |
| Kẹt ải | 90 giây không có tiến triển: thoát, tính thất bại (`PB_FLOOR_STUCK`) |
| Thời gian tối đa cả lượt | 25 phút: thoát, tính thất bại (`PB_RUN_MAX`) |

Cả 20 ải dùng chung **một** tuyến. Sang ải mới thì tool đi lại từ điểm đầu tuyến.

Trong code, "ải N" là map thứ N trong danh sách trên: ải 1 = 20060, ải 2 = 20110, …

---

## 2. Panel quyết định có chạy Doanh Trại hay không (`PbReady`, `PbWanted`, `PbArgs`)

Doanh Trại chỉ được đưa vào danh sách gửi xuống game khi đủ cả 5 điều kiện:

1. Ô `[PB] Doanh Trại` đang được tick.
2. Hôm nay chưa xong: chưa đủ số lượt/ngày, và chưa bị bỏ qua (hết lượt game, thiếu hoa, không vào được).
3. Nếu tick "Làm từ … đến …": giờ trên máy nằm trong khung. Khung được phép vắt qua nửa đêm.
4. Nếu tick "Làm khi liên trảm từ A và còn từ B phút": buff liên trảm ≥ A **và** thời gian còn lại ≥ B phút.
5. SWF không báo "pb wait" trong 60 giây gần nhất.

Khi chưa đủ điều kiện, ô trạng thái hiện "Đang chờ" và dòng "Hôm nay" ghi lý do. Trong lúc chờ, Đánh quái vẫn chạy.

Lệnh gửi xuống game:
`pb_start list=…,dt,… dt_runs= dt_rev= dt_jump=0|1 [dt_lz=ge:A:B] [dt_sf=N] [dt_nomob=N] [dt_route=x:y;…] done=…`

---

## 3. Từ thành vào phó bản

**`pbNextKey`** bỏ qua Doanh Trại nếu: đã bị skip, đã đủ số lượt, hoặc điều kiện liên trảm không đạt. Trường hợp cuối, tool báo `pb wait dt <lý do>`.

**`pbTownStep`**
- Đi về Tương Dương nếu đang ở chỗ khác. Chỉ dịch chuyển khi có VIP.
- Đi tới NPC 1656, gửi lại lệnh đi mỗi 5 giây.

**`pbOpenStep`**
1. Gửi `send_10711(1656)` rồi chờ gói 10712. Thử tối đa 4 lần, mỗi lần cách 4 giây.
2. Đọc lượt đã vào / tổng lượt của Doanh Trại. Hết lượt thì skip "hết lượt hôm nay".
3. Kiểm tra hoa: cần **số hoa ≥ "Số hoa hồi sinh"**. Thiếu thì skip "thiếu hoa hồng".
4. Gửi `send_10701([1656, 20060, 1, 1])`: thưởng thường, độ khó thấp nhất.

**`pbEnterStep`**: chờ vào map 20060. Quá 20 giây thì thử lại. Thất bại 3 lần thì skip "không vào được".

---

## 4. Trong phó bản: mỗi nhịp 100ms (`pbInStep`, phần chung với Thiên Quan)

Tool kiểm tra theo đúng thứ tự dưới đây. Gặp điều kiện nào thì xử lý điều kiện đó và dừng nhịp.

1. **Không còn ở map Doanh Trại.**
   - Về Tương Dương và đã nhận 10726: lượt hoàn thành.
   - Về Tương Dương mà chưa nhận 10726: "bị đưa ra khỏi phó bản", thất bại.
2. **Đã nhận 10726** (hoàn thành ải cuối): chuyển sang nhận thưởng (mục 8).
3. **Quá 25 phút**: thoát, thất bại.
4. **Rời khi không có quái** (nếu tick): N phút liền không thấy con quái sống nào trên toàn map thì thoát, thất bại.
5. **Vừa sang ải mới.**
   - Tool đặt lại trạng thái ải: điểm tuần về đầu tuyến, xóa cờ "cổng mở", xóa bộ đếm vòng tuần, xóa trạng thái ải chuột.
   - Log `pb floor dt k/20`.
   - Nếu tick "Dừng phó bản sau khi vượt qua ải N" và chỉ số ải ≥ N: thoát, **tính hoàn thành**. Ví dụ N = 15: vừa sang ải 16 là ra.
6. **Kẹt ải**: 90 giây ở ải này không có tiến triển thì thoát, thất bại.
7. **Ải chuột**: thấy quái có chữ "chuột" trong tên thì chuyển sang chế độ ải chuột (mục 6).
8. **Đánh.**
   - Tâm đánh là vị trí nhân vật, bán kính 20 ô.
   - Dùng hồi máu / buff theo tab Kỹ năng (`support`).
   - Nhặt đồ theo luật nhặt (`pickStep`). Trong phó bản, đồng luôn được nhặt.
   - Chọn quái gần nhất trong 20 ô. Đánh mọi loại quái, kể cả boss.
   - Có quái:
     - Nếu đang ở ải 20062 / 20067 và bật "Nhảy": thử nhảy quanh quái (mục 5). Nhảy được thì nhịp này không đánh.
     - Ngược lại: đánh.
9. **Không có quái.** Chờ đủ 3 giây yên rồi:
   - **Sang ải kế** khi có MỘT trong 3 điều kiện:
     - (a) server đã báo cổng mở (gói 10300, byte isOpen = 1) kể từ lúc vào ải;
     - (b) đi hết một vòng tuyến mà không gặp con quái nào;
     - (c) riêng Doanh Trại: thấy cổng dẫn sang map ải kế trên màn hình **và** đã đi qua ≥ 2 điểm tuần liên tiếp mà không gặp quái.
     
     Còn túi đồ cần nhặt thì nhặt trước. Sau đó gọi `mainCharWalk("<map ải kế>,-1,-1,0")` để game tự tìm cổng. Lệnh đi được gửi lại mỗi 4 giây (8 giây nếu nhân vật đang đi).
   - **Chưa sang được thì đi tuần** (`pbPatrol`):
     - Đi tới điểm tuần hiện tại, gửi lại lệnh mỗi 1,5 giây.
     - Tới điểm (≤ 2 ô) thì **đứng chờ 1 giây cho quái kịp ra**, rồi sang điểm kế. Bộ đếm "điểm không gặp quái" tăng 1.
     - Hết một vòng mà không gặp quái thì bật cờ (b).

---

## 5. Nhảy quanh quái ở ải 20062 / 20067 (`pbJump`)

Chỉ chạy khi ô "Nhảy quanh quái" được tick. Mỗi nhịp đang có quái, tool thử nhảy **trước khi đánh**.

**Điều kiện nhảy** (giống Shift+click của game, cộng thêm 2 điều kiện riêng của tool):
- Lần nhảy trước đã cách ít nhất 0,5 giây.
- Nhân vật không đang đi, không đang nhảy (cả nhảy 2, nhảy 3), không bị trói (`isSoft`).
- Thể lực `ppNow` ≥ 20.
- Map cho phép nhảy (`allowJump`), và `FightManager.isMainCharCanMove()` đúng.
- Quái cách nhân vật ≤ 7 ô.

**Chọn điểm rơi:**
- Có 12 hướng quanh quái: (±3,0), (0,±3), (±2,±2), (±4,1), (1,±4).
- Mỗi lần nhảy, hướng xoay thêm 1–3 bước ngẫu nhiên. Bỏ hướng trùng chỗ đang đứng.

**Cách nhảy:** `MainCharSeachPathManager.clear()` rồi `charJump(nhân vật, Point(x,y), -1, 500, null, false, false)`.

Kết quả là nhảy và đánh xen kẽ nhau. Khi hết thể lực (< 20) thì chỉ đánh, cho tới khi thể lực hồi lại.

---

## 6. Ải chuột (`pbMouseStep`)

- **Nhận ra ải chuột** theo tên quái có chữ "chuột", ở bất kỳ ải nào. Tool không cần biết đó là map nào.
- Bán kính đánh 200 ô (cả map). Mỗi lần chỉ đánh 1 con, con gần nhất trước.
- **Chuột chết:**
  - Chờ gói rơi đồ (11162) của **đúng con đó**, tối đa 1,5 giây.
  - Nhặt hết đồng rồi mới đánh con tiếp theo. Đồng luôn được nhặt.
- **Không còn thấy chuột:**
  - Đi tuần theo tuyến Doanh Trại đúng 1 vòng.
  - Hết vòng mà không thấy chuột, đồng đã nhặt hết và yên 3 giây: log "hết chuột và đồng", quay về đánh bình thường.
- Trong lúc ở ải chuột, tool chỉ đánh chuột. Quái thường đứng xa có thể bị bỏ lại.

---

## 7. Câu hỏi qua ải và rời phó bản (`pbConfirmDialogs`)

- Khi nhân vật bước lên cổng, server có thể gửi **10052** `[x, y, câu hỏi]` và game hiện hộp FAlert.
  - Tool ghi nhận câu hỏi trong 15 giây.
  - Tool bấm nút `FAlert.ok` (Xác nhận) và log `pb confirm qua ải: …`.
  - Doanh Trại không kiểm hoa ở bước này (chỉ Thiên Quan kiểm).
- Lúc đang đi ra cổng ra (trong 20 giây), hộp hỏi rời phó bản cũng được bấm Xác nhận.

---

## 8. Hoàn thành, nhận thưởng, ra khỏi phó bản (`pbRewardStep`, `pbExitStep`)

1. Nhận **10726**, chờ 1,5 giây, gửi `send_10723()` để nhận thưởng.
2. Sau 5,5 giây: nhặt nốt đồ quanh đó và đóng bảng phó bản.
3. Đi ra bằng `mainCharWalk("20002,-1,-1,0")`, gửi lại lệnh mỗi 6 giây. Hộp hỏi rời phó bản được xác nhận như mục 7.
4. Về tới Tương Dương: `pb end dt ok …`, panel cộng 1 lượt "hôm nay".
5. Còn lượt thì lặp lại từ mục 3. Hết lượt thì sang phó bản kế, hoặc `pb finish` nếu không còn phó bản nào.

---

## 9. Chết trong phó bản (`pbStep`, `pbRevive`)

- **Thời điểm thử hồi sinh:** lần đầu sau 2 giây, các lần sau cách thêm 8 giây mỗi lần. Tối đa 6 lần.
- **Hồi tại chỗ** (tốn hoa) khi số lần chết trong lượt ≤ "Số hoa hồi sinh" **và** còn hoa. Không thì về thành.
- **Cách bấm:** bấm nút trên bảng hồi sinh của game. Không thấy bảng thì gửi gói 20075 (0 = tại chỗ, 1 = về thành).
- **Sau khi sống lại:**
  - Còn ở map Doanh Trại: đánh tiếp ải đó.
  - Bị đưa về thành: lượt thất bại.

---

## 10. Mất kết nối / mở lại client

- Client tắt bất thường thì panel tự mở lại, không giới hạn số lần.
- Vào game mà nhân vật đang đứng trong map Doanh Trại (và ô Doanh Trại đang tick): tool chạy tiếp lượt dở trước mọi việc khác.

---

## 11. Các điểm còn yếu / chưa chắc (để bạn xem)

1. **Gói cổng mở (10300):** tool nhận bất kỳ 10300 nào có isOpen = 1, không so với cổng của ải đang đứng. Nếu server báo cổng khác, tool có thể đi tìm cổng sớm.
2. **Điều kiện (c) có thể sang ải khi quái còn sống ngoài tầm 20 ô.** Tool thấy cổng sang ải kế và đi qua 2 điểm tuần không gặp quái là đi luôn. Nếu cổng chưa mở, nhân vật sẽ đứng ở cổng cho tới khi bị tính kẹt 90 giây.
3. **20 ải dùng chung 1 tuyến 8 điểm.** Nếu bố cục các ải khác nhau, nhiều điểm có thể nằm ở chỗ không đi được, làm chậm.
4. **Các khoảng chờ:** chờ yên 3 giây, đứng 1 giây ở mỗi điểm tuần, gửi lại lệnh đi tuần mỗi 1,5 giây, gửi lại lệnh sang ải mỗi 4 giây. Các khoảng này có thể rút ngắn như đã làm cho Liên Trảm.
5. **Nhảy chỉ kiểm khoảng cách tới quái (≤ 7 ô),** không kiểm ô rơi có đi được hay không. Ô không hợp lệ thì game tự từ chối, tool thử lại sau 0,5 giây với hướng khác.
6. **Ở ải chuột tool chỉ đánh chuột.** Nếu ải đó có cả quái thường mà cổng chỉ mở khi giết hết, quái thường sẽ được đánh sau khi tool báo "hết chuột".
7. **Tính năng tool kia có mà mình chưa làm:** "Chạy ra giữa bản đồ với các ải dưới N".
8. **Kiểm hoa lúc vào chỉ so với "Số hoa hồi sinh",** không tính hoa cho câu hỏi qua ải (nếu Doanh Trại cũng tốn hoa khi qua ải).
9. **Chưa chạy trên game thật:** nhảy ở 20062 / 20067, câu hỏi 10052, bảng hồi sinh, ải chuột.
