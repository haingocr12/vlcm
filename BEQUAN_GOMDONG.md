# Nhận lợi ích bế quan & "Gom đồng – Ninh Hoàn" (chuyển đồng giữa các acc)

Nguồn: `Main.as` trong `game.pak` (đã giải chuỗi). Tên hàm là tên đã bị làm rối trong bản decompile, ghi lại để đối chiếu.

---

## 1. Nhận lợi ích bế quan (tự động)

### 1.1 Bật / tắt
- Cờ bật là `§46§`, **mặc định `true`**. Mình không tìm thấy ô tick nào trên giao diện để tắt cờ này, nên chức năng **luôn chạy**.
- Các handler được đăng ký lúc khởi động:
  - `method_18(50674, autoBeQuan_received50674, "auto_bequan_received_50674")`
  - `method_18(50554, autoBeQuan_received50554, "auto_bequan_received_50554")`

### 1.2 Luồng xử lý

| Bước | Chiều | Gói | Nội dung |
|---|---|---|---|
| 1 | server → client | **50674** | báo bế quan đã đủ thời gian; bot log "[BẾ QUAN] đã đủ 12 giờ hãy nhận lợi ích." |
| 2 | client → server | **50553** | `[byte dùngBùa]`: 1 nếu trong túi có "bùa bế quan", 0 nếu không |
| 3 | server → client | **50554** | `[int kinhNghiệm][int chânKhí]`: kết quả nhận |

### 1.3 `autoBeQuan_received50674` (line 113388)
```
nếu §46§ == false                       -> bỏ qua
nếu getTimer() - lầnTrước < 15000 ms     -> bỏ qua (chặn lặp 15 giây)
attr = mainChar.data.attributeInfo
gqNow = attr.gqNow ; gqMax = attr.gqMax
nếu gqMax <= 0                           -> bỏ qua
lầnTrước = getTimer()
coBua = false
duyệt goodsInfo.goodsBagArr:
    nếu res.name (chữ thường) chứa "bùa bế quan" và count > 0 -> coBua = true; dừng
gửi 50553 [byte coBua ? 1 : 0]
log "[BẾ QUAN] đã đủ 12 giờ hãy nhận lợi ích."
lỗi -> log "[BẾ QUAN] Lỗi: …"
```
Ghi chú:
- Bot **không** so `gqNow` với `gqMax`. Nó tin hoàn toàn vào việc server đã gửi 50674.
- Bot **không tự dùng** bùa bế quan. Nó chỉ báo cho server biết là có bùa (byte = 1). Mình chưa xác định được bùa làm thay đổi phần thưởng ra sao.

### 1.4 `autoBeQuan_received50554` (line 113693)
```
đọc int kinhNghiem, int chanKhi
log "[BẾ QUAN] Nhận thành công: <kinhNghiem> kinh nghiệm | <chanKhi> chân khí."
lỗi -> log "[BẾ QUAN] Lỗi: …"
```

---

## 2. "GOM ĐỒNG – NINH HOÀN": chuyển đồng giữa các acc qua sạp hàng

### 2.1 Mục đích
Gom đồng từ nhiều acc phụ về một acc:
- **Acc bán** (acc gom) treo **1 Ninh Hoàn giá 50.000 đồng** trên sạp cá nhân.
- **Acc mua** (acc phụ) mua lại món đó.

Mỗi lần mua, 50.000 đồng chuyển từ acc mua sang acc bán. Đây không phải bán đồ cho NPC.

### 2.2 Giao diện (`§2C§`, line 110277)

| Phần | Thành phần |
|---|---|
| Tiêu đề | "GOM ĐỒNG - NINH HOÀN" |
| **ACC BÁN** | "ID acc của bạn:"; ghi chú "Tự tách [Ninh Hoàn] còn 1 và treo 50.000 đồng. Shop đủ 10 ô thì chờ."; nút **Bật bán** (`§91§`) / **Tắt bán** (`§+#§`); dòng trạng thái |
| **ACC MUA** | "Nhập ID acc BÁN cần mua:" (chỉ nhận số); ghi chú "Chỉ mua khi đồng > 60.000 và đúng giá 50.000 đồng."; nút **Bật mua** (`§^!§`) / **Tắt mua** (`§7@§`); dòng trạng thái |
| Chân trang | "Giá cố định: 50.000 đồng / 1 Ninh Hoàn. Acc mua không mua nếu còn <= 60.000 đồng." |

Vòng lặp chạy theo timer **800 ms** (`§=C§ = new Timer(800)`).

### 2.3 Phía acc bán: `§%>§` (line 111305)
```
đếm số món đang treo trên sạp
nếu đã treo >= 10 -> "Trạng thái: Shop đầy 10/10 - đang chờ trống." ; chờ
tìm trong túi một chồng Ninh Hoàn:
    không có -> "Trạng thái: Không còn Ninh Hoàn trong túi."
    nếu chồng có count > 1 -> TÁCH ra 1 cái:
        gửi 11183 [short position][int 1]
        "Trạng thái: Đang tách 1 Ninh Hoàn..."
    nếu có chồng count == 1 -> TREO lên sạp:
        gửi 13001 [short position][int 50000 (đồng)][int 0 (bạc)][short 0]
        "Trạng thái: Treo 1 Ninh Hoàn = 50.000. Shop <n>/10"
```

### 2.4 Phía acc mua
**Hỏi sạp**: `§7+§` (line 111502)
```
sellerId = ID acc bán đã nhập ; <= 0 -> "Trạng thái: ID acc bán không hợp lệ."
copper = mainChar.data.moneyInfo.copper
nếu copper <= 60.000 -> "Trạng thái: Đồng <= 60.000, tạm dừng mua. Hiện có <copper>"
gửi 13011 [int sellerId]       // xem sạp của acc bán
"Trạng thái: Đang kiểm tra shop ID <sellerId>..."
```

**Đọc sạp và mua**: `§,%§` (line 111616), xử lý dữ liệu sạp server trả về
```
đọc: tên sạp (UTF), số món (byte), rồi từng món ...
tìm món có tên "Ninh Hoàn" / "[Ninh Hoàn]" với giá đúng 50.000
    không có -> "Trạng thái: Shop ID <id> chưa có Ninh Hoàn giá 50.000."
kiểm lại đồng của mình
gửi 13003 [int roleID người bán][short vị trí ô trên sạp][int itemID]
          [int giá đồng][int giá bạc][int số lượng = 1]
"Trạng thái: Đã gửi mua 1 Ninh Hoàn giá 50.000 từ ID <roleID>."
lỗi -> "Lỗi đọc shop: …"
```

### 2.5 Bảng gói tin

| Cmd | Chiều | Dùng cho | Nội dung |
|---|---|---|---|
| 11183 | gửi | acc bán | tách chồng: `[short vị trí ô túi][int số lượng tách = 1]` |
| 13001 | gửi | acc bán | treo lên sạp: `[short vị trí ô túi][int giá đồng 50000][int giá bạc 0][short 0]` |
| 13011 | gửi | acc mua | xem sạp: `[int sellerId]` |
| 13003 | gửi | acc mua | mua món trên sạp: `[int roleID][short vị trí ô sạp][int itemID][int đồng][int bạc][int số lượng]` |

### 2.6 Nhận xét
- Giá và vật phẩm **viết cứng**: chỉ dùng Ninh Hoàn, giá 50.000 đồng, mức đồng tối thiểu của acc mua là 60.000.
- Acc mua kiểm cả **tên** lẫn **giá đúng 50.000** trước khi mua, để không mua nhầm món đắt nếu sạp bị đổi.
- Sạp giới hạn 10 ô. Acc bán chờ khi đầy, và treo tiếp khi acc mua đã mua bớt.
- Rủi ro: nhiều acc liên tục mua cùng một món giá cố định từ một acc là dấu hiệu chuyển tiền rất rõ, dễ bị nhà phát hành phát hiện.

---

## 3. Phụ lục: bán đồ cho NPC trong bot gốc (chỉ có "Đĩnh Vàng")

Bot gốc **không có** chức năng bán nhóm "đồ cổ" cho NPC. Chỗ duy nhất bán đồ cho NPC là `method_184` (line 49739), chạy khi bấm nút **"Mua liên châu tiễn"** hoặc trong timer tự train lại:

1. Mua 2000 cái vật phẩm id 31301 (nhiều khả năng là Liên Châu Tiễn) từ NPC 1739: gói **10881** `[1739][31301][2000]`.
2. Duyệt túi: món nào tên chứa **"Đĩnh Vàng"** thì bán cho NPC 1739. Mỗi món một gói **10883** `[int 1739][int id vật phẩm][int 1][short vị trí ô túi]`.
