# Gói ra chiêu gửi thẳng lên server & cơ chế tra vật phẩm

Phân tích từ `Main.as` (SWF trong `game.pak`, đã giải chuỗi). "Gửi thẳng" nghĩa là gọi `NetWorkManager.sendMsg(cmd, ByteArray)` (hàm `method_22`), không đi qua luồng UI/Pipe của client.

## 1. Gói ra chiêu gửi thẳng

Ở tầng mạng chỉ có **2 lệnh ra chiêu**, được dùng lại ở nhiều nơi:

| Cmd | Ý nghĩa | Payload (big-endian) |
|---|---|---|
| **10083** | dùng skill **lên một đối tượng** | `int skillId`, `byte targetType`, `int targetId`, `double getTimer()` |
| **10283** | dùng skill **tại một ô trên bản đồ** | `int skillId`, `short tileX`, `short tileY`, `double getTimer()` |

`targetType` lấy từ `sceneChar.type`: người chơi `1` là giá trị viết cứng trong gói Thích khách. Còn `getTimer()` là số mili-giây kể từ khi Flash chạy, đóng vai trò timestamp phía client.

### 1.1 Nhánh dự phòng khi "5 skill thanh phím tắt" thất bại

Đây là những gói được gửi **khi `train_useQuickbarFirst5Skill` trả về false** (không có skill hợp lệ, đang hồi chiêu, hoặc gọi lại chưa quá 100 ms):

| Nơi gọi | Gói | skillId | Mục tiêu / tọa độ |
|---|---|---|---|
| `PhoBan_15`, `PhoBan_15a`, `PhoBan_20a`, `PhoBan_50` → `method_skill(10283)` | **10283** | **53021** (cố định) | tọa độ **của chính mình** (`mainChar.tile_x/y`) |
| `PhoBan_15a`, `PhoBan_20a`, `PhoBan_50` → `method_skill2(10083)` | **10083** | theo môn phái `partyID`: 1→**51013**, 2→**51023**, 3→**51033**, 4→**51043**, 5→**51053**, 6→**51063** | **chính mình** (`mainChar.type`, `mainChar.id`) |
| Tự làm nhiệm vụ, giết quái nhiệm vụ: hàm `§'E§` (line ~8116) | **10283** | theo môn phái: 0/không phái→**53021**, 1→51013 … 6→51063 | tọa độ **con quái** (`monster.tile_x/y`); sau đó mới thử `train_useQuickbarFirst5Skill` |

Ghi chú:
- Trong nhánh nhiệm vụ, trước khi gửi 10283 bot còn phát Pipe `CHANGED_LOCKEDCHAR_BASE_ATTR` và `HIT_CHAR [monster, true, true]` để client khóa mục tiêu và tự đánh.
- Skill `5xxx3` trong `method_skill2` gắn với chính mình, còn trong `§'E§` lại đặt vào ô của quái, dù cùng một bộ ID.

### 1.2 Các chỗ khác gửi thẳng lệnh ra chiêu (không phải nhánh dự phòng)

Mình liệt kê để đủ danh sách:

| Nơi gọi | Gói | skillId | Mục tiêu |
|---|---|---|---|
| Vòng train `method_192` / `method_192a` (sau khi `§]A§` chọn được skill) | 10083 | skill chọn từ 5 ô quickbar | quái hoặc người chơi gần nhất |
| `method_76(skill)`: dùng skill tùy loại `res.target` | 10083 (qua `method_94`) | `skill.id` | `target == 11`: chính mình (buff); còn lại: mục tiêu đang khóa (`method_102`, dùng `var_121`) |
| `method_76`, trường hợp `res.target == 8` | 10083 (qua `method_242`) | `skill.id` | chính mình (tham số tọa độ được truyền vào nhưng không được ghi) |
| `method_192_buff6003_likeClass12` | 10083 | **6003** | chính mình: buff khi có vợ/chồng (`spouseID`) ở gần, không phải thủ hộ |
| PK Train `§^$§` / `pkInlineSkill` (line 67947) | 10083 | skill PK đã chọn | người chơi mục tiêu |
| Thích khách `§,2§` (line ~90100) | 10083 | 0/không phái→**53021**, 1→**51012**, 2→51022, 3→51032, 4→51042, 5→**51052**, 6→51062 | `targetType = 1` (người chơi), `targetId` = mục tiêu nhiệm vụ Thích khách |
| `skillcachkhong1` → `skillcachkhong1_useSkill(52006…52009)` | qua `method_52`, không phải `method_22` | 52006 → 52007 → 52008 → 52009 | kiểm tra `isLearn` và `isCooling` trước khi dùng |
| `method_940` | 10083 | tham số | có trong mã nhưng **không có chỗ gọi** (mã chết) |

### 1.3 Gói liên quan (không phải ra chiêu)

| Cmd | Dùng để | Payload |
|---|---|---|
| 50583 | bật/tắt auto treo máy gốc của game (`afkInfo2`) | `byte 0/1` |
| 10271 | tự nâng cấp skill (`autoSkillCQ0_send`) | `int skillId`, `byte loại` (môn phái / bang / giang hồ) |
| 10881 | **mua đồ ở shop NPC** | `int npcId`, `int goodsId`, `int số lượng`, ví dụ `1030, 30301, 1`; `1739, 31301, 2000`; `1639, goodsId, n` (mua Tuyết Hoàn/Thủy Đơn) |

Còn khoảng 70 cmd khác mà bot gửi thẳng (nhận quà, phó bản, vận tiêu, đổi kênh…). Mình không liệt kê ở đây vì không liên quan đến ra chiêu.

---

## 2. Bot có tra vật phẩm trực tiếp trong game không?

**Với danh sách nhặt đồ thì không.** Bot không có giao diện tìm kiếm hay chọn vật phẩm từ cơ sở dữ liệu game. Người dùng phải **tự gõ tên**. Chuỗi được lưu vào `var_184`, sau đó so khớp **lúc chạy** với tên hiển thị trên mặt đất của vật rơi (`sceneChar.headFace.nickName`), chứ không so với ID vật phẩm.

Thay vì tra cứu, bot đi kèm hai thứ:

### 2.1 Danh sách mặc định có sẵn (`method_279` / `method_279a`)
Khi ô danh sách trống, bot điền sẵn chuỗi sau:
```
[Mảnh],[Đồng],[Hoa Hồng Đỏ],[Hoa Hồng Vàng],[Hoa Hồng Lam],[Hoa Hồng Lục],[Hoa Hồng Trắng],[Hoa Hồng Đen],
[Mảnh Vân Thạch],[Thông Lam],[Chân Khí Đơn],[Tôn Chân Khí],[Chân Long Đồng Nhân],[Thông Lục],[Đá May Mắn],
[Kinh Mạch Đồng Nhân],[Đá Kim Cương],[Đá Tinh Luyện],[Đá Thăng Cấp],[Bồ Đề Đơn],[Bích Linh Đơn],[Luyện Cốt Đơn],
[Thưởng Phạt Lệnh],[Đá Dưỡng Thiên],[Kinh Nghiệm Đơn x2],[Phỉ Thúy (loại 1)],[Hồng Ngọc (loại 1)],[Mã Não (loại 1)],
[Bùa Mở Rương],[Tiến Cấp Phù],[Long Thú Cân],[Bia Ám Khí],[Tụ Linh Châu],[Mảnh nguyên liệu cao cấp],[Tụ Pháp Đơn],
[Exp Thú Cưỡi x2],[Đại Chân Khí],[Chân Khí x2],[Bách Lộ],[Liên Lộ],[Bích Đơn],[Nông Đơn],[Vân Hoàn],[Bách Hoàn],
[Tuyết Hoàn],[Thủy Đơn],[Thạch Lộ],[Tục Mệnh Đơn],[Hồi Thể Đơn],[Thông Tím],[Đá Ám Khí],[Thăng Đoạn Thạch],
[Đá Tăng Tốc-Tiêu],[Tư Chất Đơn],[Long Châu],[Quả Sung]
```
Nút "Sửa" / "Đồng ý" (`method_138`) cho phép sửa tay. Danh sách được lưu theo từng nhân vật qua `save_state` / `load_state` (file `data\state_*`).

### 2.2 Từ khóa nhóm cài sẵn trong `method_170`
Mỗi từ khóa được bỏ ngoặc `[...]` và khoảng trắng, rồi khớp **chuỗi con, không phân biệt hoa thường**. Riêng các nhóm sau khớp bằng regex:

| Từ khóa | Khớp |
|---|---|
| `[Mảnh Bí Kíp]` | `^Mảnh\s(Hấp Tinh Đại Pháp\|Hóa Công Đại Pháp\|…\|Hồi Xuân\|Hồi Phục)$`, khoảng 70 tên bí kíp/kỹ năng |
| `[Mảnh Trận Pháp]` | `^Mảnh\s(Lưỡng Nghi\|Tam Tài\|Hổ Dực\|Thái Ất\|Phong Thỉ\|Lưu Vân\|Thiên Canh\|Phục Hi)$` |
| `[Hoa Hồng]` | `^Hoa\sHồng\s(Đỏ\|Vàng\|Lam\|Lục\|Trắng\|Đen)$` |
| `[Đồng]` | `^\d+\sđồng$`, tức tiền rơi trên đất |
| `[Mảnh]` | xử lý riêng theo tiền tố "Mảnh" |

### 2.3 Những chỗ bot *có* tra dữ liệu vật phẩm của game (nhưng không dùng cho nhặt đồ)

| Hàm | Làm gì |
|---|---|
| `§0;§(goodsId)` | `GoodsResManager.getGoodsRes(id).name`: đổi ID ra tên, chỉ dùng để **ghi log** (`§,%§`, `§;"§`) |
| `§+$§(name)` | lấy danh sách `function_shop` của NPC **1639**, gọi `getGoodsRes` cho từng món, tìm món có tên chứa `name` và trả về `goodsId`. Dùng khi **tự mua thuốc** (Tuyết Hoàn, Thủy Đơn) rồi gửi gói 10881 |
| `§5=§` | duyệt túi `goodsInfo.goodsBagArr`, đọc `goods.res.name` để **tự dùng đồ trong túi** (ví dụ "Kinh Nghiệm Đơn x2") |
| `§4G§` | `getGoodsRes(115101)` → hiển thị skin vũ khí **Du Long Đao** trên nhân vật; chỉ đổi hiển thị phía client, không phải tra cứu |

**Kết luận:** bot có sẵn quyền truy cập `GoodsResManager` (bảng dữ liệu vật phẩm của game) và dùng nó cho việc mua đồ, dùng đồ và ghi log. Tuy vậy, **tính năng nhặt đồ không dùng cơ sở dữ liệu này**. Nó chỉ so khớp văn bản tên hiển thị với danh sách người dùng gõ tay, cộng với danh sách mặc định và vài nhóm regex cài sẵn. Hệ quả là gõ sai chính tả hay sai dấu tiếng Việt thì sẽ không nhặt được, và bot cũng không kiểm tra được tên đó có thật trong game hay không.
