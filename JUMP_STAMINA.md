# Bot gốc nhảy mà không tốn thể lực như thế nào?

Nguồn: `Main.as` trong `game.pak` (đã giải chuỗi).

## 1. Bot gốc có hai kiểu nhảy hoàn toàn khác nhau

| Kiểu | Dùng ở | Cách gọi | Kiểm thể lực trong bot? |
|---|---|---|---|
| **Nhảy quanh quái** | PB25 (map 20062/20067), PB30 (boss Khôi Khôi) | Hàm nhảy của client: `MainCharSeachPathManager.charJump(mainChar, Point, -1, jumpMax, null, false, false)` | Không (bot không đọc thể lực ở đâu cả) |
| **Nhảy khi train** / **nhảy khi PK train** | Train thường, PK train | **Gửi thẳng gói 10063** qua `NetWorkManager.sendMsg`, **không gọi hàm nhảy của client** | Không |

Trong toàn bộ `Main.as` **không có** chỗ nào đọc `ppNow`, `pp`, `sp` hay bất kỳ chỉ số thể lực nào. Bot gốc không quản lý thể lực.

## 2. "Nhảy khi train": gói 10063 nhảy tại chỗ

Checkbox giao diện: **" nhảy khi khi train "** (biến `var_nhay1`).
Hàm `§2+§` (line 78478) chạy theo timer **700 ms** (`new Timer(700)`):

```
nếu nhân vật đang WALK hoặc chưa tick "nhảy khi train" -> bỏ qua
nếu nhân vật đang có 1 trong 15 buff sau -> bỏ qua:
    80202261 80201167 80201575 80201678 80201850 80202851 80202402 80202444
    80202812 80202824 80202829 80202849 80203087 80203326 80203483
x = mainChar.tile_x ; y = mainChar.tile_y
gửi 10063: [short x][short y][short x][short y][byte 1][double getTimer()]
```

Bản PK (`§6D§`, line 94698) gửi đúng gói đó khi đang PK train (`var_pkTrain1`, `§+8§`) và nhân vật không đang đi. Mình chưa thấy hàm này được gắn vào timer nào, nên có thể đây là mã chết.

### Vì sao nhiều khả năng không tốn thể lực
Phần này là **suy luận**: mình không có mã của client game (`TGame.tse`) và không thấy được phía server.

1. **Bỏ qua lớp client.** Khi người chơi nhảy (Shift+click), client đi qua `charJump`. Nhiều khả năng chính hàm này kiểm tra thể lực đủ hay không, chọn kiểu nhảy (nhảy 1/2/3 tầng), chạy hoạt ảnh và trừ thể lực phía client. Gửi thẳng 10063 thì bỏ qua toàn bộ lớp đó.
2. **Điểm đầu = điểm cuối.** Gói 10063 có cấu trúc `[từ x, từ y, tới x, tới y, kiểu, thời gian]`, giống một gói di chuyển/nhảy. Bot gửi **từ (x,y) tới chính (x,y)**, tức nhảy 0 ô. Nếu server tính thể lực theo quãng đường, hoặc chỉ trừ khi vị trí thực sự đổi, thì cú nhảy 0 ô không tốn gì.
3. **Byte kiểu = 1.** Có thể là "nhảy 1 tầng", loại rẻ nhất hoặc miễn phí.

Mục đích có lẽ là để server ghi nhận trạng thái "đang nhảy" (né đòn, hoặc kích hoạt hiệu ứng liên quan tới nhảy) **trong khi nhân vật vẫn đứng đánh tại chỗ**. Danh sách 15 buff ở trên nhiều khả năng là các trạng thái mà lúc có chúng thì không nên/không cần nhảy (đang bị khống chế, hoặc đã có buff do nhảy tạo ra). Mình không tra được tên các buff vì bảng dữ liệu buff nằm trong client game, không có trong `game.pak`.

## 3. "Nhảy quanh quái" (PB25/PB30) có tốn thể lực không?

Kiểu này đi qua `charJump` của client, nên **về lý thuyết vẫn tốn thể lực như người chơi nhảy bình thường**. Bot chỉ không tự kiểm thể lực trước khi gọi. Hết thể lực thì `charJump` tự từ chối, và bot thử lại sau 0,5 giây.

Mình chưa biết ý nghĩa của các tham số `-1`, `jumpMax`, `false`, `false`, vì cần mã client game. Nếu bạn thấy trong game bot gốc nhảy ở PB25 mà thể lực không giảm, nguyên nhân nằm trong `charJump` của client với bộ tham số này. Muốn xác nhận thì phải có `TGame.tse`, hoặc thử trực tiếp: gọi `charJump` với `-1 / false / false` và so `ppNow` trước và sau.

## 4. Gợi ý kiểm chứng (cho tool của bạn)

1. Log `mainChar.data` (thể lực hiện tại, `ppNow`) trước và sau khi gửi `10063 [x,y,x,y,1,t]`. So với một lần Shift+click nhảy bình thường.
2. Thử `10063` với điểm cuối khác điểm đầu 1–3 ô, để xem thể lực có bị trừ theo quãng đường không.
3. Thử byte kiểu = 2, 3, để xem kiểu nhảy ảnh hưởng tới chi phí thế nào.
4. Lưu ý rủi ro: gửi gói nhảy thô đều đặn mỗi 700 ms với điểm đầu = điểm cuối là một mẫu rất dễ bị server phát hiện. Nhảy bằng `charJump` an toàn hơn.
