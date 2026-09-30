# Chức năng nhảy trong phó bản Doanh Trại (PB25)

> **Lưu ý tên phó bản:** trong bot, chuỗi giao diện ghi **"Phó Bản 25(Doanh Trại)"**. Tức là Doanh Trại là **PB25**, không phải PB20. PB20 **không có** chức năng nhảy: không có lệnh `charJump` nào trong `PhoBan_20` / `PhoBan_20a`. Tài liệu này phân tích chức năng nhảy của PB25.

Nguồn: `Main.as` (SWF trong `game.pak`, đã giải chuỗi). Các hằng số được đối chiếu với **P-code** xuất bằng JPEXS (`-format script:pcode`), vì bản decompile AS3 làm mất mảng hướng nhảy.

## 1. Bật / tắt

| Biến | Mặc định | Nguồn |
|---|---|---|
| `var_nhaypb1` | `true` | checkbox **" Nhảy khi đi phó bản 25"** (tên control `nhaypb`), lưu/đọc theo nhân vật qua `save_state` / `load_state` |
| `pb25JumpAroundEnable` | `true` | cờ nội bộ, không có trên giao diện |

Hai cờ phải cùng bật thì bot mới nhảy.

## 2. Được gọi ở đâu

Trong `PhoBan_25` (line ~41638), khi đã có quái mục tiêu (`var_3000`), mỗi tick chạy:

```
if (pb25_tryJumpAroundMonster(mob)) return;          // nhảy được -> tick này không đánh
if (train_useQuickbarFirst5Skill(mob, mob, null)) return;
... (dự phòng: gói ra chiêu thô, xem PACKETS_AND_ITEMS.md)
```

**Nhảy được ưu tiên trước đánh.** Tick nào đã nhảy thì bỏ lượt đánh. Vì mỗi lần nhảy cách nhau ít nhất 500 ms, nhân vật sẽ xen kẽ **nhảy → đánh → nhảy → đánh** quanh con quái.

## 3. Hàm `pb25_tryJumpAroundMonster(mon)` (line 108736)

### 3.1 Điều kiện (không thỏa thì `return false`, chuyển sang đánh)
1. `var_nhaypb1` và `pb25JumpAroundEnable` đều bật.
2. **Map hiện tại là `20062` hoặc `20067`**. Chỉ hai map này trong danh sách map PB25 mới nhảy (xem 3.4).
3. `mon` và `mainChar` khác null.
4. **Chặn tần suất:** `getTimer() - pb25JumpAroundLastMs ≥ 500 ms`.
5. Nhân vật không ở trạng thái `WALK` hay `JUMP`, và `FightManager.isMainCharCanMove()` trả về true (không bị choáng/trói).
6. **Quái đủ gần:** khoảng cách Euclid theo ô từ nhân vật tới quái **≤ 7 ô**.
7. Không đang nhảy: `mainChar.isJumping()`, `on2Jumping()`, `on3Jumping()` (nhảy tầng 1/2/3) đều false.

### 3.2 Tầm nhảy
```
jumpMax = 8
if (OtherConst.JUMP_MAX_DIS > 0) jumpMax = OtherConst.JUMP_MAX_DIS   // hằng số của game
```

### 3.3 Chọn điểm đáp (12 hướng quanh con quái)
Mảng offset `(dx, dy)` tính theo ô, lấy nguyên văn từ P-code (`newarray 12`):

| # | dx | dy | | # | dx | dy |
|---|---|---|---|---|---|---|
| 0 | +3 | 0 | | 6 | −2 | +2 |
| 1 | −3 | 0 | | 7 | −2 | −2 |
| 2 | 0 | +3 | | 8 | +4 | +1 |
| 3 | 0 | −3 | | 9 | −4 | +1 |
| 4 | +2 | +2 | | 10 | +1 | +4 |
| 5 | +2 | −2 | | 11 | +1 | −4 |

Tức là 4 hướng thẳng cách 3 ô, 4 hướng chéo (±2, ±2), và 4 điểm xa hơn cách khoảng 4 ô.

Thuật toán:
```
for tryCount = 0 .. 11:
    pb25JumpAroundDir = (pb25JumpAroundDir + 1 + int(random()*3)) % 12   // bước 1..3 hướng, ngẫu nhiên
    nx = mon.tile_x + offsets[dir].dx
    ny = mon.tile_y + offsets[dir].dy
    if (nx, ny) == vị trí hiện tại: thử tiếp
    if distance(hiện tại, (nx, ny)) > jumpMax: thử tiếp        // ngoài tầm nhảy
    MainCharSeachPathManager.clear()                             // hủy đường đi hiện tại
    MainCharSeachPathManager.charJump(mainChar, Point(nx,ny), -1, jumpMax, null, false, false)
    pb25JumpAroundLastMs = now
    return true
return false                                                      // 12 hướng đều không hợp lệ
```
- `pb25JumpAroundDir` được giữ giữa các lần gọi, nên hướng nhảy **xoay vòng quanh quái**, mỗi lần lệch ngẫu nhiên 1–3 hướng, khó đoán.
- Tâm nhảy là **vị trí con quái**, không phải vị trí nhân vật. Điểm đáp luôn cách quái 3–4 ô, vẫn trong tầm đánh của phần lớn skill.
- Bot không kiểm tra ô đáp có đi được hay không. Việc đó giao cho `charJump` của game xử lý.

### 3.4 Map áp dụng
`pb25Maps = [20060,20110, 20061,20111, 20062,20112, 20063,20113, 20064,20114, 20065,20115, 20066,20116, 20067,20117, 20068,20118, 20069,20119]`

Danh sách này gồm từng cặp map `2006x` / `2011x`, nhiều khả năng là hai phiên bản của cùng một ải. Nhảy chỉ bật ở **20062** và **20067**. Nếu mỗi cặp là một ải thì đó là **ải thứ 3 và thứ 8**. Hai map `20112` / `20117` của cùng cặp **không** được bật nhảy.

## 4. Cơ chế ở tầng game / mạng

- Bot **không tự gửi gói nhảy**. Nó gọi `com.tgame.manager::MainCharSeachPathManager.charJump(...)`, tức hàm nhảy có sẵn của client (giống người chơi bấm nhảy). Client tự chạy animation và gửi gói di chuyển/nhảy như bình thường.
- Tham số `jumpMax` lấy từ hằng số của game (`OtherConst.JUMP_MAX_DIS`), nên không vượt quá tầm nhảy hợp lệ.
- Rủi ro bị phát hiện chủ yếu nằm ở **nhịp độ**: nhảy đều đặn khoảng mỗi 0,5 giây, xen kẽ với đánh, và luôn đáp ở các offset cố định quanh quái.

## 5. Hàm tương tự
`pb30_tryJumpAroundMonster` (line 109400) có cấu trúc gần giống, dùng cho PB30. Ngoài ra còn các tùy chọn " nhảy khi khi train " và " Nhảy khi đánh Boss Khôi Khôi" (`chk_jumpBossKhoi`) cho train thường và boss. Các phần này chưa phân tích ở đây.
