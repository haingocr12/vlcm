# Cách tool dùng kỹ năng và nhặt đồ khi train quái

Phân tích từ `game.pak` sau khi giải mã (xem `REPORT.md`) và decompile bằng JPEXS. Toàn bộ logic nằm trong lớp `Main` của SWF, số dòng ghi bên dưới là số dòng trong `Main.as` đã decompile.

## 0. Gỡ lớp làm rối chuỗi

Mọi chuỗi trong SWF đều bị giấu qua lời gọi `_e_-----_._e_-_-__-(n)`. Lớp giải mã dùng 3 khối `DefineBinaryData`:

| Khối | Nội dung |
|---|---|
| `3__e_-_-_--.bin` (4 byte) | số XOR cho chỉ số, int little-endian, `= -1820302793` |
| `2__e_-----.bin` (65 byte) | byte đầu là số khóa (4), tiếp theo là 4 khóa AES-128 × 16 byte |
| `1__e_--_-.bin` | `int` số chuỗi (1992), rồi lặp `[int len][len byte]` |

Chuỗi thứ *i* = **AES-128-ECB** (khóa `keys[i % 4]`, PKCS7), đọc dạng UTF-8. Lời gọi `f(n)` trả về `strings[n ^ xorKey]`. Sau khi thay 5 630 lời gọi bằng chuỗi thật, tên lớp game, tên sự kiện và log tiếng Việt đều đọc được.

Các đối tượng game mà bot lấy qua `getDefinitionByName`:

| Biến | Lớp trong game |
|---|---|
| `var_190` | `com.tgame.common::GameInstance` (mainChar, scene, lockOnChar…) |
| `var_131` | `com.tgame.manager::NetWorkManager`: `sendMsg(cmd, ByteArray)` gửi **gói tin thô** |
| `var_377` | `com.tgame.manager::PipeManager`: phát sự kiện nội bộ của client (như người dùng thao tác UI) |
| `var_71` | `com.tgame::PipeConstants` (`USE_SKILL`, `PICK_UP`, `CHANGED_LOCKEDCHAR_BASE_ATTR`, `HIT_CHAR`…) |
| `var_373` | `com.zcp.manager::CDFaceManager`: `isCooling(skillId)` |
| `var_248` | `com.tgame.manager::FightManager`: `isMainCharCanHit()` |
| `var_340` | trình di chuyển: `mainCharWalk("mapId,x,y,0", …)` |

Hai hàm gửi mà bot dùng:
- `method_48(pipeName, obj)` gọi `PipeManager.sendMsg`: đi qua luồng bình thường của client.
- `method_22(cmd, bytes)` gọi `NetWorkManager.sendMsg`: **gửi thẳng gói tin lên server**, bỏ qua UI.

---

## 1. Vòng lặp train

Có hai timer: `method_192` (line 65977) và `method_192a` (line 68674, bản mới hơn). Mỗi tick chạy các bước sau:

1. **Kiểm tra điều kiện dừng/ưu tiên**: chết, đang về NPC mua thuốc (`train_goNpcDrug`), đang sửa đồ (`train_hasBrokenEquip1`), đang tạm dừng F4… Nếu có việc ưu tiên thì `return`.
2. **Ưu tiên nhặt đồ trước khi đánh**:
   `pickup_goods = method_190()` (mục 3). Nếu có đồ thì đi tới nhặt rồi `return`. Nếu không có đồ thì mới đánh.
3. **Giữ bán kính train**: điểm train `var_77` được lưu khi bấm bắt đầu (`method_39(mainChar)`), bán kính là `var_45` (mặc định 15 ô). Khoảng cách được tính bằng `method_54(p1,p2) = floor(Point.distance × var_148)`, trong đó `var_148 = sqrt(TILE_WIDTH × TILE_HEIGHT)`.
   Nếu nhân vật đi xa quá `(var_45 + 2)` ô thì bật cờ `var_153` và `mainCharWalk` về điểm train. Khi không còn `sceneChars` thì log "[Train] Không có sceneChars -> quay lại điểm train".
4. **Chọn mục tiêu**: duyệt `scene.sceneCharacters`:
   - bỏ qua: chính mình, `!usable`, trạng thái `DEATH`
   - bỏ qua: con nằm ngoài `(var_45 + 1)` ô tính từ **điểm train** (không tính từ vị trí hiện tại, nên bot không bị kéo đi xa)
   - bỏ qua: loại quái đặc biệt `MOUNT`, `SHENFU`, `QINGGONGYAN`, `HUIYAN_JINGYANSHU`, `TUNSHITIANDI_MONSTER`, `XUYUANSHU`/`ZHONGQIUJIE`/`DENGMI` (quái sự kiện), `BIWUZHAOQIN_XIUQIU`…
   - người chơi (`PLAYER`) chỉ được tính khi bật `var_pkTrain1`. Khi đó xét thêm `pkMode`, cùng phe `zhenyingID`, đồng bang `isTongbang` (tùy `var_pk_tongbang`), danh sách bỏ qua `var_pk_ignore_names`, và không PK trong thành Tương Dương (`XIANGYANGCHENG_MAP_ID`)
   - kết quả: con quái gần nhất `target_monster` / `target_monster_min_distance` và người chơi gần nhất `target_char` / `target_char_min_distance`
5. **Tắt auto gốc của game**: nếu `afkInfo2.isStart` (auto treo máy có sẵn của client) đang bật, bot gửi **gói 50583** `[byte 0]` (`method_89(0)`) để tắt. Hai cơ chế auto không chạy chồng lên nhau.
6. **Đánh**: `_skill = §]A§(target, distance)` (mục 2.2). Nếu `FightManager.isMainCharCanHit()` cho phép:
   - `PipeManager.sendMsg(CHANGED_LOCKEDCHAR_BASE_ATTR, target)`: khóa mục tiêu trên UI
   - `method_94(skillId, target.type, target.id)`: **gói 10083** (mục 2.1)
   - lưu `§05§ = getTimer()` làm mốc lần ra chiêu cuối

---

## 2. Dùng kỹ năng

### 2.1 Gói tin ra chiêu (gửi thẳng lên server)

| Hàm | Cmd | Payload |
|---|---|---|
| `method_94(skillId, tType, tId)` (line 62747) | **10083** | `int skillId, byte targetType, int targetId, double getTimer()` |
| `method_skill(cmd)` (line 47646) | **10283** | `int 53021, short myTileX, short myTileY, double getTimer()`: chiêu **53021 đánh tại chỗ theo tọa độ bản thân** (AoE quanh mình) |
| `method_skill2(10083)` (line 47681) | **10083** | chọn skillId theo môn phái `partyID`: 1→51013, 2→51023, 3→51033, 4→51043, 5→51053, 6→51063…; target là **chính mình** (`mainChar.type`, `mainChar.id`), tức **buff môn phái** |
| `skillcachkhong1_useSkill(52006…52009)` (line 97552) | 10083 | chuỗi skill "cách không" 52006→52009 |

Trường `double getTimer()` là timestamp phía client, được gửi kèm mỗi gói ra chiêu.

### 2.2 Chế độ "Dùng 5 skill đầu thanh phím tắt" (`var_auto5skill1`)

**Lấy danh sách skill**: hàm `§7!§()` (line 105806).
```
for i = 1..5:
    s = mainCharData.skillInfo.getSkillByQuickbarindex(i)
    lấy nếu: s.id > 0, s.id != 50000 (đánh thường), s.isLearn == 1, s.res.useway == 1 (skill chủ động)
```

**Chọn skill theo khoảng cách**: hàm `§]A§(target, dist)` (line 93541).
- Xoay vòng **round-robin** bằng con trỏ `§`&§`, thử tối đa `ids.length` lần.
- Bỏ skill khi `!isLearningSkill(id)` hoặc `CDFaceManager.isCooling(id)` (đang hồi chiêu).
- Đọc `skill.res` / `skill.buffres`:
  - `res.target == 3 / 4` là skill nhắm địch. Với `buffres.user_scope == 1/2` và `aoe_type == 2`, skill được coi là AoE.
  - `res.target == 6 / 9` là skill vùng/đất.
  - Chỉ chọn skill khi `res.distance × var_148 ≥ dist`, tức **tầm skill đủ tới mục tiêu**.
- Trả về skill hợp lệ đầu tiên. Nếu không có thì `null`, và vòng train sẽ đi lại gần mục tiêu.

**Bản dùng trong phó bản/nhiệm vụ**: hàm `train_useQuickbarFirst5Skill(finalTarget, targetMonster, targetChar)` (line 106049).
- Chặn spam: `if (now - §05§ < 100) return false`, tức **tối đa khoảng 10 lần/giây**.
- Nếu 5 ô đầu không có skill hợp lệ thì log "[Train Skill] 5 ô đầu không có skill đánh hợp lệ." (tối đa 1 lần/3 giây).
- Ra chiêu **qua Pipe (luồng UI của client)**, không gửi gói thô:
  ```
  PipeManager.sendMsg(CHANGED_LOCKEDCHAR_BASE_ATTR, target)   // khóa mục tiêu
  PipeManager.sendMsg(USE_SKILL, skillObj)                    // như bấm phím skill
  ```
  Vì vậy client vẫn tự kiểm tra tầm, mana, animation.

**Tổ hợp trong phó bản** (`PhoBan_15a/20a/50…`):
```
if (!train_useQuickbarFirst5Skill(mob)) {
    method_skill(10283);    // chiêu 53021 AoE tại chỗ
    method_skill2(10083);   // buff môn phái
}
```
Khi dùng 5 skill thất bại (hồi chiêu, không đủ tầm), bot rơi xuống **gửi gói thô** ra chiêu AoE và buff.

---

## 3. Nhặt đồ

### 3.1 Cấu hình (UI "Nhặt vật phẩm")
| `var_98` | Chế độ |
|---|---|
| 1 | " Không nhặt" (`pickup_none`) |
| 2 | " Chỉ nhặt các vật phẩm sau:" (`pickup_specified`). Danh sách lấy từ ô text `var_184`, phân cách dấu phẩy, lưu vào `pickup_list_arr` |
| 3 | nhặt tất cả (`pickup_all`) |

### 3.2 Chọn món cần nhặt: `method_190()` (line ~73000)
```
nếu var_98 == 1: return null
tách var_184 -> pickup_list_arr (chỉ khi chuỗi thay đổi)
best = null; min_index = 999999999
for each c in scene.sceneCharacters:
    bỏ qua mainChar, !usable
    bỏ qua nếu blacklist[c.id] > getTimer()      // §;A§: id -> thời điểm hết cấm
    bỏ qua nếu c.type == BAG
    bỏ qua nếu dist(điểm_train, c) > var_45 ô    // chỉ nhặt trong bán kính train
    nếu var_98 == 2:
        i = vị trí đầu tiên trong pickup_list_arr mà method_170(tên, c) khớp
        không khớp -> bỏ
        ưu tiên i nhỏ hơn (đứng trước trong danh sách);
        cùng i -> ưu tiên gần nhân vật hơn
    nếu var_98 == 3: chọn món gần nhân vật nhất
return best
```
`method_170(needle, c)` so tên `c.headFace.nickName` với từ khóa như sau:
- Bỏ khoảng trắng/tab ở đầu và cuối, đổi cả hai về chữ thường (`toLowerCase`), rồi khớp **chuỗi con** (`itemLow.indexOf(needleLow) >= 0`).
- Từ khóa đặt trong `[...]` được coi là **nhóm đặc biệt**. Ví dụ `[Mảnh Trận Pháp]` khớp regex `^Mảnh\s(Hấp Tinh Đại Pháp|Hóa Công Đại Pháp|…|Hồi Phục)$` gồm khoảng 70 tên mảnh trận pháp/bí kíp. Người dùng chỉ cần gõ một từ khóa là nhặt được cả nhóm.

### 3.3 Đi tới và nhặt: `method_330()` và phần đầu của `method_192a`
```
d = |myX - gx| + |myY - gy|            // khoảng cách Manhattan theo ô
nếu d > 1: mainCharWalk("map,gx,gy,0")  // đi tới ô chứa đồ
PipeManager.sendMsg(PICK_UP, goods.id)  // nhặt qua luồng client
```
Nhặt được gọi **mỗi tick** trong lúc đi, nên khi đến đủ gần là nhặt ngay.

### 3.4 Chống kẹt
| Tình huống | Xử lý |
|---|---|
| Server trả "Ngươi không thể nhặt vật phẩm này" (đồ của người khác, túi đầy…) | `blacklist[id] = true` (cấm vĩnh viễn trong phiên) rồi log "quay lại train." |
| Đồ nằm quá xa điểm train (`tooFarGoods`) | `blacklist[id] = true` |
| Train thường: đứng một chỗ mà không nhặt được sau vài lần | đánh dấu `id = "bugged_<id>"`, đặt tọa độ -999, bỏ qua |
| `pb_tryPickupLikeTrain1` (phó bản): kẹt quá 5 giây | `blacklist[id] = now + 1000`, bước lệch 1 ô (`x+1`) để gỡ kẹt, log "[PB] Vật phẩm … kẹt > 5s -> blacklist, đi tiếp." |
| `§2#§` (PB25/PB30): kẹt quá 6 giây | `blacklist[id] = now + 10000`, bước lệch 1 ô, log "… kẹt > 6s -> bỏ qua tạm 10s." Có chặn gửi `PICK_UP` quá dày (< 200 ms). |

Trong phó bản, khi nhặt xong bot **cập nhật lại điểm train `var_77` = vị trí hiện tại**, để vòng đánh tiếp theo lấy chỗ mới làm tâm.

---

## 4. Nhận xét

- **Thứ tự mỗi tick**: an toàn (thuốc, sửa đồ) → nhặt đồ → chọn mục tiêu → ra chiêu. Bot **luôn nhặt xong mới đánh tiếp**.
- **Kỹ năng**: dùng dữ liệu của chính client (quickbar, `isCooling`, `res.distance`) để chọn chiêu hợp lệ, xoay vòng 5 ô đầu. Chỉ khi thất bại mới dùng gói thô 10283/10083 với skill ID cứng (53021, 5x0x3 theo môn phái). Phần gói thô này là chỗ dễ bị server phát hiện nhất: skill ID cố định, timestamp client, gửi dồn không qua kiểm tra UI.
- **Nhặt đồ**: lọc theo danh sách tên có thứ tự ưu tiên, giới hạn trong bán kính train, blacklist có thời hạn để không kẹt vĩnh viễn.
- Vì bot chạy **bên trong** client Flash và gọi thẳng các manager của game, nó không cần đọc/ghi bộ nhớ hay giả lập chuột. Đổi lại, mỗi bản cập nhật client có thể làm hỏng tên lớp và field, nên tác giả phải phát hành lại `game.pak` qua cơ chế tự update.
