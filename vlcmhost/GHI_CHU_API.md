# GHI CHÚ API — VLCM (Mộng Chí Tôn, TePayLink)

Cập nhật: 03-10-2026 (bản 03-10e: không bỏ quái aggro, bỏ giới hạn giờ tool tự đặt; 03-10d: thứ tự phó bản, nhảy mọi ải Doanh Trại theo tầm game, Mê Cung leo tầng/treo quái, áp dụng cài đặt ngay, đi tuần không bị buff chặn; còn lại trong danh sách chờ sửa). Gửi file này (hoặc cả vlcmhost_train.zip) ở đầu mỗi cuộc trò chuyện mới.
Ký hiệu: **[đã xác minh]** = chạy được trên game thật hoặc đọc rõ trong mã TGame; **[mock]** = mới chạy trên game giả lập;
**[đoán]** = suy ra, chưa kiểm tra.

---

## 1. Kiến trúc

| Thành phần | Vai trò |
|---|---|
| vlcmhost.exe | C++/ATL, **chỉ Win32** (Flash.ocx 32-bit). Đăng nhập HTTP, nhúng Flash, nạp VlcmLoader.swf. Tham số `--acc <user>`. Pipe `\\.\pipe\vlcmhost-<user>` (ký tự lạ → `_`). Tiêu đề cửa sổ `VLCM Host - <user> - S<sv> ...` |
| VlcmLoader.swf | AS3 (VlcmLoader + VlcmTrain + VlcmUtil). Nạp TGameLoader, tự chọn kênh/nhân vật, nhận lệnh `vlcm_command(cmd,args)` |
| vlcmpanel.exe | GUI Win32, nối pipe. **Chỉ 1 tool nối 1 host** (panel hoặc vlcmctl) |
| data\accounts.xml | `<acc><cong/><user/><pass/><sv/><kenh/><nv/><hidden/></acc>`; pass = `ENC:` + base64(DPAPI) hoặc chữ thường |
| data\train_<acc>.ini | cài đặt panel mỗi tài khoản |

Build: SWF bằng Royale mxmlc (`-compiler.compress=false`, giao file FWS). Panel/ctl bằng MinGW hoặc VS.

## 2. Đăng nhập (vlcmhost) [đã xác minh, Fiddler 24/09]

1. `POST /api/v2/User/GetToken` `{"username","password","device":"Windows"}` → `token` (status=1)
2. `POST /api/v2/User/login` `{"username","token","device":"Windows"}` → `sessionId`
3. `GET /api/gamepage/gamepage.asp?ServerID=..&SessionId=..&Device=windows` → `var parameters {...}` (cần auth, sign, config, main, game)
4. Movie = `parameters.game` (TGameLoader.swf) + toàn bộ parameters nối vào query (encodeURIComponent). Thiếu `config` → Flash treo cứng.

Game gọi ra host qua ExternalInterface: `console.log`, `setTips(text)` (hiện khi đóng), `showPopup`, `window.open`.

## 3. Luồng chọn kênh / nhân vật (VlcmLoader) [đã xác minh]

- Kênh: gói **40300** → đọc từ `position=4`: `ubyte count`, mỗi kênh `byte id, UTF host, UTF port("9002,9003"), UTF name, int count`.
  `GameConfig.lineID/lineName/lineServerIP/lineServerPortArr/lineServerPort` rồi gửi **10013** (lấy nhân vật) qua `loginSocket`.
- Nhân vật: gói **10014**. Tên có thể dạng `[x]tên` (server gộp) → so phần sau `]` ở cả hai phía.
  Chỉ 1 nhân vật → luôn chọn (từ 30-09e).
- Vào game: `LoginRoleVO` (roleID, nickName = chuỗi gốc, headImg = `HeadResChange.headResChangeToString(head)`, level, partyID, sex),
  `GameConfig.loginRoleVO = vo`, `GameState.hasSelectedChar = true`, `ProcessManager.enterLine()`, **sau đó** `FacadeManager.killFacade(PipeConstants.STARTUP_LOGIN)`.
- **10022** = đã vào game. Mất kết nối: theo dõi `NetWorkManager.lineSocket.connected` (2s); không đăng ký lại handler — `ReloginManager` của game tự vào lại.
- `removeMsg` có thể ném TypeError nếu opcode hết observer → tự giữ cờ + try/catch.

## 4. Lớp game (KNOWN, tên đầy đủ)

| Tên ngắn | Tên đầy đủ |
|---|---|
| NetWorkManager | com.tgame.manager::NetWorkManager (`registerMsg(op, fn, ctx)`, `removeMsg`, `sendMsg(op, ByteArray, socket?)`, `loginSocket`, `lineSocket`) |
| GameConfig / GameState | com.tgame.common::GameConfig / ::GameState (`hideOtherPlayerHpEffect`) |
| ProcessManager / FacadeManager | com.tgame.manager::… |
| PipeConstants | com.tgame::PipeConstants |
| LoginRoleVO | com.tgame.common.vo.login::LoginRoleVO |
| HeadResChange | com.tgame.common.staticdata::HeadResChange (trên game giả lập tìm thấy ở com.tgame.util::HeadResChange — loader tự tìm theo tên ngắn) |
| GameInstance | com.tgame.common::GameInstance (`mainChar`, `mainCharData`, `scene`, `lockOnChar`, `stage`) |
| PipeManager | com.tgame.manager::PipeManager (`sendMsg(name, body)`) |
| MainCharSeachPathManager | com.tgame.manager::MainCharSeachPathManager |
| MapTransManager | com.tgame.common.res.map::MapTransManager (`getMapRes(map).isFuben==1` = phó bản, `getSceneNameBySceneId`, `getSmallestSceneId`) |
| ExchangePositionManager | com.tgame.manager::ExchangePositionManager (`checkItemShortIndex(n)` → Skill/Goods ở ô phím n) |
| CDFaceManager | com.zcp.manager::CDFaceManager (`isCooling(id)` — chỉ hồi chiêu riêng) |
| GoodsResManager | com.tgame.common.res.goods::GoodsResManager (`getGoodsRes(id).name`) |
| Item_MsgSenderProxy | com.tgame.moudels.Item.model::Item_MsgSenderProxy (tự `new`, chỉ dùng hàm `send_`) |
| MainUI_MsgSendProxy | com.tgame.moudels.mainui.model::MainUI_MsgSendProxy |
| ItemFace | com.tgame.common.ui.componets::ItemFace |

Nếu tên đầy đủ đổi sau cập nhật game, loader tìm theo tên ngắn và báo trong sự kiện `classes`.

## 5. Thao tác (đi qua luồng có sẵn của client)

| Việc | Gọi | Ghi chú |
|---|---|---|
| Đi (cả khác map) | `mainCharWalk("map,x,y,0", false, null, true, true, false)` | [đã xác minh] |
| Đi tới NPC | `mainCharWalk(idNPC)` | [đã xác minh] |
| Đánh thường | `PipeManager.sendMsg("HIT_CHAR", [quái, true, true])` | ≤ 1 lần/giây |
| Dùng skill | `sendMsg("USE_SKILL", skill)` | skill hỗ trợ: đặt `lockOnChar = mainChar` trước, trả lại sau [mock] |
| Bấm phím tắt | `sendMsg("KEY_n")` | ≤ 10 lần/giây |
| Nhặt | như game bấm vào túi: `mainCharWalk("map,x,y,0", false, MoveCallBack, true, false)`; `onMoveArrived`/`onMoveUnable` → `sendMsg("PICK_UP", idTúi)` **một lần** | phải đứng **đúng ô** túi, gửi khi còn xa → "cách vật phẩm quá xa" [đã xác minh]. MoveCallBack = com.zcp.engine.vo.move::MoveCallBack. Chờ hết `"attack"` (≤1.5s) |
| Dùng vật phẩm | `sendMsg("ITEM_USE", [new ItemFace(goods), 1])`; dự phòng `send_11201([id, ô])` | |
| Hủy | `Item_MsgSenderProxy.send_11181([ô, 0])` | |
| Bán cho NPC | `send_10883([npc, id, sốLượng, ô])` | Vũ Khí hoặc Tạp Hóa |
| Sửa đồ | `send_50221(npc)` rồi ~600ms `send_50223([1 thường / 2 đặc biệt, npc])` | tại Vũ Khí 1638 |
| Hồi sinh về thành | `NetWorkManager.sendMsg(20075, [byte 1, double getTimer()])` | như nút "Về thành hồi sinh" |
| Truyền tống VIP | `MainUI_MsgSendProxy.send_10131({id: map})` → nhận **10132** (`position=4`, `readInt id`) → `send_10133({id})` | **chỉ khi `attributeInfo.isVIP == 1`**; không bao giờ dùng loại tốn tpoint [đã xác minh]. Game cũng mở bảng hỏi chi phí `MapPortalPanel` (modal, khóa bàn phím) trong `POPWindowManager` → đóng bằng `POPWindowManager.closeWindow()` (đúng nút Hủy) |
| Đóng cửa sổ như Esc | duyệt `GameInstance.uiInstance.uiContainer` từ trên xuống: `FPanel` → `dispatchEvent(new FCloseEvent("close", null))`; `BasePanel` → `onClose()`; bỏ qua `uiInstance._cardGiftPanel` | theo KeyboardManager (phím 27). FPanel = com.fireice.panel::FPanel, FCloseEvent = com.fireice.event::FCloseEvent, BasePanel/POPWindowManager = com.tgame.common.ui.componets:: |
| Cài đặt game | `sendMsg("SETTING_DATA", [idx, bool])` | bảng mục 8 [mock] |

## 6. Dữ liệu đọc

- Nhân vật: `mainChar.tile_x/tile_y`, `getStatus()` (`"death"`, `"attack"`), `data.attributeInfo.isDeath()`.
- `mainCharData.attributeInfo`: `hpNow, hpMax, mpNow, mpMax, lv, isVIP, bagCount`. Ô trống = `bagCount*30 - goodsBagArr.length`.
- Tiền: `mainCharData.moneyInfo.copper`.
- Map hiện tại: `scene.mapConfig.mapID`.
- Scene: `scene.getCharsByType(2)` = quái (`c.usable`, `c.data.res.type`: 1 thường, 2 tinh anh, 3 boss); `(6)` = NPC; `(11)` = túi đồ rơi. `getCharByID(id, type)`.
- Skill: `mainCharData.skillInfo.getSkillByID(id)`; `s.res`: `name, distance (ô; px = ×25), useway (1 = chủ động), public_type, depletion_parameter (2 → skillWaste() là MP), effect_ids (id buff), target (1/2 = lên bản thân)`.
  Hồi chiêu (Fight_proxy.playMainCharSkillCoolingtime / playMainCharPublicCoolingtime) [đọc mã game]:
  skill vừa dùng hồi **max(getCoolingtime, getPublicCoolingtime)** — `CDFaceManager.isCooling(id)` thấy;
  mỗi skill KHÁC cùng `getPublicCoolingtimeClass()` ("type_publictype") bị khóa **getPublicCoolingtime() của chính skill đó** tính từ lúc ra chiêu —
  isCooling KHÔNG thấy (mặt CD chung có itemID null) → tool tự khóa từng skill (+150ms). `skillInfo.skillObj` = mọi skill đã học. Kỹ năng cơ bản 50000 bỏ qua.
- Buff: `mainCharData.buffInfo.hasBuff(id)`, `getBuff(id)`; VIP = buff **1300 / 80000**, hạn ở `vipTime` dạng `"d-m-yyyy  h:mm"`.
- Túi đồ: `mainCharData.goodsInfo.goodsBagArr` → Goods: `id, position (ô), count, quality, strengthen_grade (sao), bind (1 = khóa), isShowDestroyPanel, curr_durability`;
  `g.res`: `kind (2 = trang bị), grade, limit_grade (cấp nhân vật cần để mặc), popsinger (phái), position (loại ô), durability, f_discard_is_ui`.
  Ý nghĩa `res.discard`, `res.is_sale`: **chưa rõ** (chờ file `data\tui_<acc>.txt` từ game thật).

## 7. Gói tin nhận

| Opcode | Cấu trúc (sau 4 byte opcode) |
|---|---|
| 11160 | thêm túi: `int id, short x, short y, int goodsId, int money` |
| 11162 | thêm túi do quái rơi: `int mobId`, rồi như 11160 |
| 11164 | xóa túi: `int id` |
| 22222 | thông báo: `UTF type, UTF msg` (đọc từ `position=4`). Khi nhặt: "Vật phẩm của người khác, thử nhặt lại sau 15 giây"; "Cách vật phẩm quá xa, không thể nhặt" [đã xác minh] |
| 10132 | (game) `int id, UTF name, short restrict, UTF monst, UTF level, UTF boss, UTF time, UTF item, int expand` → mở MapPortalPanel |

Không có trường chủ sở hữu trong gói túi đồ. Luôn trả `position = 4` sau khi đọc.

## 8. Bảng mã

- **Phẩm chất**: 0–1 Trắng, 2 Lam, 3 Lục, 4 Tím, 5–6 Vàng.
- **Phái (popsinger)**: 1 Thiếu Lâm, 2 Toàn Chân, 3 Cổ Mộ, 4 Đào Hoa.
- **Loại ô (res.position)**: 1 Vũ khí, 2 Vũ khí thú cưỡi, 3 Áo, 4 Uyển; 5 Đai lưng, 6 Giày, 7 Mũ, 8 Dây chuyền, 9 Nhẫn, 10 Vòng tay, 11 Ngọc bội, 12 Áo choàng **[đoán 5–12]**.
- **Nhóm skill theo id**: 51xxx môn phái, 52xxx giang hồ, 53xxx bang phái, còn lại khác.
- **Skill hỗ trợ**: Cách Không Độ Khí, Từ Hàng Phổ Độ (hồi máu); Chiến Ý Kích Ngang, Võ Thần Lâm Thể (buff).
- **SETTING_DATA idx**: 0 ẩn nhân vật khác, 1 ẩn tên, 2 rung màn hình, 3 hiệu ứng nhịp tim, 4 theo dõi nhiệm vụ, 5 bảng tổ đội, 6 nhạc nền, 7 âm thanh,
  8 chặn chat riêng, 9 chặn kết bạn, 10 từ chối giao dịch, 11 từ chối mời bang, 12 từ chối mời tổ đội.
- **Map / NPC**: Tương Dương 20002 (NPC Vũ Khí **1638**, Tạp Hóa **1739**). Danh sách đầy đủ: Maps_PB.xml, Maps_Pro.xml, Redirect.xml, Drugs.xml, Skills.xml (bạn đã gửi).
- Thuốc: id trong Drugs.xml; bình nhỏ 30101–30104 (HP), 30201–30204 (MP) dùng làm mặc định.

## 9. Lệnh pipe (panel → host), trả `> ok ...` / `> err ...`; sự kiện `! <tên> <chi tiết>`

- Host: `ping`, `reload`, `quit` (thoát gọn, từ 30-09e).
- Train: `where`, `status`, `train_start k=v...` (map x y r skillmode=sets|keys g1..g4 heal=id:pct buff= supmp= keys= pick=off|list|all picklist= types= restdeaths= restmin=), `train_stop`, `skills` (`id:tên:tầm:useway:ptype:ptime`), `bag_names`.
- Util: `util_set k=v...` (closeui pot hpkey mpkey hp mp hpids mpids repair rtype bh floorq floors freee freei tele vllauto gs=idx:0|1,... hpfx protect rules), `util_pause on=0|1`, `bag_dump`, `bh_stop`, `bh_reset`, `stats_reset`, `render mode=show|hide1|hide2`, `perf_reset`.
- Quy tắc bán/hủy (mỗi dòng): `act=sell|destroy;real=0|1;kind=equip|item;q=;s=;lv=;p=;pos=;lock=locked|unlocked;names=A|B`.
- `status` trả: `state map x y hp mp lv name mapname kills deaths picked vip vipexp copper free dur mpick msold mrep mlost prefused since upause render perf bh bhn trip`.

## 10. Ràng buộc đã thống nhất

Không né anti-cheat, không gói tin bất thường, không khai thác server, không vượt qua từ chối của client (món `isShowDestroyPanel` luôn được bảo vệ),
không PvP tự động, **không bao giờ dùng truyền tống tốn tpoint**. Log vlcmhost_debug.log chứa token — không đăng công khai.
Quy trình: trao đổi → chốt → làm một lần; chỉ gửi file đã đổi; hướng dẫn viết tiếng Việt có dấu.

## 11. Việc còn treo

- Xác minh trên game thật: toàn bộ phó bản (đặc biệt câu hỏi 10052, bảng hồi sinh, cổng ra), nhận biết buff, tác dụng SETTING_DATA, ẩn mức 1/2, cột CPU, chọn nhân vật khi tên có `[x]`, đóng cửa sổ lúc vào game, nhặt đồ kiểu mới, khóa hồi chung từng skill.
- `res.discard` / `res.is_sale`: chờ file túi đồ. Đồ cổ: chờ danh sách tên.
- Khác: lọc tên pipe trong panel; tính năng tổ đội (làm sau); tuyến riêng từng tầng Thiên Quan (khi bạn gửi).

### 11.1 Chờ sửa — CHỈ SỬA KHI HAIN BẢO (03-10) [đọc mã, chưa sửa]
Rà soát các phó bản còn lại theo các lỗi đã sửa ở Thiên Quan. Phần dùng chung đã có ở mọi phó bản:
- tầm đánh/tiến lại gần;
- bỏ mục tiêu khi máu không giảm, boss không bỏ;
- hồi máu/buff không chen lúc đi;
- giới hạn lệnh đi 0,7 giây;
- canh kẹt khi đang đi;
- nhặt đúng ô + Space;
- chống nhắm vào ngựa chiến;
- nhóm kỹ năng/phạm vi riêng;
- số lượt NPC.

Doanh Trại dùng chung luồng `pbInStep` với Thiên Quan.

**Doanh Trại**
- B1. Ải chuột có thể kẹt vĩnh viễn:
  - `approach` bỏ con chuột không tới được, nhưng `pbMouseStep` gán lại `run.mouseTarget` ngay;
  - lúc chọn chuột không bỏ qua `_black`;
  - `progressAt` luôn được cập nhật nên không thoát theo "kẹt tầng".
  
  Sửa: bỏ con đó và bỏ qua `_black` khi chọn.
- B2. Nhảy (`pbJump`):
  - **[đã sửa 03-10a]** nhảy ở mọi ải Doanh Trại (bỏ `PB_JUMP_MAPS`); giữ điều kiện game: thể lực ≥ 20, không bị trói/định thân, `MapRes.allowJump`.
    Ải game không cho nhảy: bỏ qua, log 1 lần `info pb map <id> game không cho nhảy`. Ải chuột vẫn không nhảy (luồng chuột riêng).
  - **[03-10d]** Tầm nhảy Doanh Trại: theo game `OtherConst.JUMP_MAX_DIS` (không đọc được thì 8), bỏ điểm đáp ngoài tầm như bot gốc.
    Chế độ thử 500 (03-10b) và log thống kê nhảy **đã bỏ** theo yêu cầu Hain. Phu Tử (boss Khôi Khôi) giữ 500 như cũ.
    OtherConst: tên gói **[đoán]** `com.tgame.common::OtherConst`, sai thì loader tìm theo tên ngắn.
  - còn treo: nhảy mỗi 0,5 giây gọi `MainCharSeachPathManager.clear()`, chen vào lúc đánh/tiến lại gần;
    đề xuất: chỉ nhảy lúc rảnh, hoặc giãn ra 2–3 giây (chờ chốt), và ghi vào log "spam đi". Liên quan B3 (nhảy mọi ải → B3 dễ gặp hơn).
- B3. **Bỏ quái gần, chạy đánh quái xa** (Hain báo 03-10). Nghi theo thứ tự:
  1. nhảy làm `approach` tưởng 1,5 giây không lại gần → cấm con gần 10 giây / 10 phút;
  2. luật giữ mục tiêu: lỡ chọn con xa thì giữ tới khi chết;
  3. quay lại boss sau khi nhặt.
  
  Đề xuất:
  - log mỗi lần chọn mục tiêu (id, cách bao nhiêu ô, số con gần bị bỏ vì cấm);
  - không tính "không lại gần được" lúc đang/vừa nhảy;
  - đang đi tới con xa mà có con trong 3–4 ô thì đổi (trừ boss).

**Liên Trảm**
- C1. Đi tới boss (cách > 6 ô) bằng `walkTo` mỗi 0,5 giây, không kiểm kẹt. Boss sau vật cản thì spam đi tới khi hết 25 phút. Sửa: dùng `fight`/`approach` như Thiên Quan (boss không bỏ, đi không nhích thì ra chiêu).
- C2. Boss bất tử/đếm số: chưa xử lý (xem câu hỏi 1 ở cuối mục).
- Giữ nguyên: không chờ đồ rơi (giữ nhịp chuỗi); chỉ nhặt khi bật chế độ nhặt.

**Mê Cung**
- D1. Cửa (`mcStep`):
  - phải đứng đúng ô cửa, 4 giây không sang thì đổi cửa → có thể đánh dấu nhầm cửa đúng là sai, nếu game không chuyển map như ở Thiên Quan;
  - nhân vật dừng cách 1 ô thì gửi lệnh đi mỗi giây mãi mãi.
  
  Sửa: ≤ 2 ô và đứng 2 giây → gửi 10051; 3 giây sau vẫn ở phòng cũ mới đổi cửa; đi tới cửa có kiểm kẹt.
- D2. Phòng thần bí: đi các điểm và ra cổng không kiểm kẹt. Sửa: dùng `goPoint`/`badPoint` và ra cổng như D1.
- D3. Treo đánh ở tầng chỉ định: không chờ đồ rơi sau khi giết. Sửa: thêm `dropWaiting`.

**Mê Cung — leo tầng / treo quái** (Hain báo 03-10) — **[đã sửa 03-10c]**
- Đã làm: leo tầng không đánh / không nhặt (bỏ mục tiêu, đi thẳng tới cửa); tầng treo `mcFarmStep`: vùng (76,51) bán kính 6, `fight` + `pickStep` (cài đặt nhặt Đánh quái), hết quái thì về (76,51);
  ngưng treo `mc_farmby=min` (`mc_farmmin`) hoặc `lz` (`mc_farmlz`, lzCount() >= X; an toàn: lượt còn < 8 phút thì thôi treo); `mc_skip=1` bỏ qua ải chuột tầng 15 + phòng thần bí (`mcSecretExit`).
  Panel: "Ngưng treo khi" (Hết số phút / Đạt mốc liên trảm), "Số phút", "Mốc liên trảm", ô "Bỏ qua ải chuột và phòng thần bí". Tuyến MeCung.xml không còn dùng cho tầng treo.
- Hain muốn:
  - leo tầng **không đánh quái**;
  - tới đúng tầng chỉ định mới **ra giữa map** treo quái;
  - option mới: treo tới khi **đạt mốc liên trảm chỉ định** thì ngưng treo, đi tiếp hoàn thành Mê Cung.
- Code hiện tại (`mcStep`):
  - leo tầng vẫn đánh quái trong 5 ô ("tự vệ") và nhặt túi trong 20 ô;
  - ở tầng treo: đánh quái trong phạm vi, hết quái thì đi theo tuyến `mc_route` (thường rỗng), không ra giữa map;
  - chỉ dừng treo theo số phút (`mc_farmmin`).
- **Đã chốt (03-10):**
  - leo tầng **không đánh gì cả**, kể cả tự vệ (quái ra liên tục): cứ đi thẳng tới cửa;
  - tầng treo: đi tới **(76,51)** (chung mọi tầng), đứng đó, quái tự tới thì đánh (không đi xa đuổi quái);
  - ngưng treo: **chọn 1 trong 2** — theo số phút (như hiện tại) **hoặc** đạt mốc liên trảm X; đạt thì đi tiếp hoàn thành Mê Cung.
- Dự kiến khi sửa:
  - tầng treo chỉ đánh quái trong tầm đánh quanh (76,51); bị đẩy xa > 3 ô thì quay về;
  - panel: ô chọn "Ngưng treo khi: hết N phút / đạt liên trảm X";
  - pb_start thêm `mc_farmby=min|lz` và `mc_farmlz=X`.
- **Chốt thêm (03-10):**
  - leo tầng **không nhặt gì**;
  - ải chuột tầng 15 / phòng thần bí: giữ như cũ, **thêm ô tick "bỏ qua ải chuột và phòng thần bí"** → không đánh, đi thẳng tới cổng/cửa;
    - Hain xác nhận: tầng 15 ải chuột và phòng thần bí **không cần giết hết vẫn qua được** → bỏ qua là đi thẳng, không cần phương án dự phòng;
  - tầng treo: nhặt đồ rơi theo **cài đặt nhặt của Đánh quái** (pick off/list/all + danh sách), trong phạm vi quanh (76,51).

**Áp dụng cài đặt ngay khi sửa trên panel** (Hain báo 03-10) — **[đã sửa 03-10d]**
- Đã làm: panel `PbLive` (sửa khung phó bản, kỹ năng, hỗ trợ, nhặt, "áp dụng cho acc khác" lúc đang chạy phó bản) → gom 1s → `pb_list live=1`;
  SWF `pbList`: áp `applyFightCfg` chung + thay `_pbRun.cfg` bằng cài đặt mới + áp bộ kỹ năng riêng; log `pb cài đặt mới (áp dụng ngay cho <k>): <mục đổi>`.
  Chỉ số tuyến / thời điểm bắt đầu treo nằm trong `_pbRun` nên không bị đặt lại.
- Hain muốn: **mọi** thay đổi trên panel được cập nhật cho tool ngay, kể cả lúc đang treo / đang chạy phó bản.
- Hiện tại (đọc code panel):
  - Tiện ích (thuốc, sửa đồ, bán/hủy, cài đặt game…): đã gửi `util_set` ngay khi sửa → có hiệu lực ngay.
  - Đánh quái (điểm train, loại quái, nhặt…) và bộ kỹ năng: panel chạy lại `train_start` sau 1,5s → có hiệu lực ngay, **nhưng chỉ khi không chạy phó bản**.
  - Phó bản (mọi ô trong khung "..." của từng phó bản) + kỹ năng/nhặt dùng trong phó bản: **chỉ lưu file**, tool chỉ nhận khi có `pb_start`/`pb_list`; lượt đang chạy giữ cài đặt cũ tới hết lượt.
- Dự kiến khi sửa:
  - panel: sửa bất kỳ cài đặt phó bản / kỹ năng / nhặt lúc đang chạy phó bản → gom 1 giây rồi gửi `pb_list` (không dừng phó bản);
  - SWF `pb_list`: cập nhật cả cài đặt của lượt đang chạy (`_pbRun.cfg`) và áp lại bộ kỹ năng/hỗ trợ/nhặt ngay; log 1 dòng "đã cập nhật cài đặt: …" liệt kê mục đổi;
  - các mục đổi giữa chừng phải an toàn: tuyến/điểm tuần giữ chỉ số hiện tại; mốc liên trảm / số phút treo xét lại ở nhịp kế (hạ dưới mức hiện tại thì ngưng ngay); số phút tính từ lúc bắt đầu treo.

**Đi tuần bị buff chặn** (Hain báo 03-10, log Thiên Quan "điểm (74,57) không tới được" 2s sau buff) — **[đã sửa 03-10d]**
- Nguyên nhân: buff → game `stopMove`; `moveTo` chặn gửi lại cùng đích 4s nếu "đã nhích kể từ lệnh trước"; `goPoint` 1,5s không lại gần đã bỏ điểm.
  `moving()` chỉ coi là đang đi khi status walk hoặc lệnh < 4s → đường dài, status nháy khác "walk" là buff chen vào.
- Sửa: `trackMove` (lần cuối đổi ô); `moving()` = walk, hoặc còn lệnh đi chưa tới (> 2 ô) và (lệnh < 1,5s hoặc vừa đổi ô < 1,5s);
  `moveTo` chỉ chặn gửi lại khi đang đi thật (walk / đổi ô trong 1s); buff/heal xong xóa `_mv`, `run.moveAt = 0`, lùi mốc `gp.prog` 1s;
  `goPoint` chỉ báo "không tới được" khi đã gửi lệnh đi ≥ 1,5s trước và 2,5s không lại gần.
- Còn khả năng: Thiên Quan dùng 1 tuyến chung 13 tầng — nếu (74,57) vẫn báo hỏng mà không có buff ngay trước thì do tuyến (cần tuyến riêng tầng đó).

**Bỏ qua quái đã aggro, tool tự thoát theo giờ** (Hain báo 03-10) — **[đã sửa 03-10e]**
- Luật 20s máu không giảm: chỉ đếm lúc đang đánh thật (không `_oor`, không walk, không bị khống chế, không vừa buff/heal); chốt cứng 45s từ lúc chọn mà máu chưa giảm lần nào. Phó bản bỏ 10s (train 60s). Log `bỏ quái #id cách … ô: lý do`.
- "Không lại gần được": quái ≤ 6 ô (`AP_NEAR`) hoặc đã di chuyển ≥ 2 ô từ lúc chọn (aggro) → không bỏ, `_forceId` ra chiêu thẳng; phó bản bỏ tối đa 10s (train vẫn 10s rồi 10 phút); log mỗi lần; `_black`/`_blackN` xóa khi sang tầng.
- Chọn được quái lúc đang đi tuần (`run.patrolling`): `MainCharSeachPathManager.clear()` + `stopMove()` rồi đánh.
- Bỏ mọi giới hạn giờ tool tự đặt: không còn "quá 25 phút"; "kẹt ở tầng / mê cung" 90s → `pbUnstick` (xóa điểm hỏng, quái bị bỏ, đi tuần lại), không thoát;
  Thiên Quan treo máy 3 phút → thôi treo, tự tìm quái (đi thêm một vòng trống thì được treo lại); Mê Cung treo theo liên trảm: không còn mốc chừa giờ.
  Giới hạn thời gian phó bản của game (Hain chưa chắc 1 tiếng, tính từ lúc vào): để game tự đẩy ra.
- Nhặt trước đánh sau: giữ nguyên (Hain xác nhận không phải nguyên nhân).

**Kéo thả thứ tự ưu tiên chức năng trên panel** (Hain báo 03-10, chờ lệnh sửa)
- Hain muốn: kéo thả các dòng chức năng ở khung ĐIỀU KHIỂN (5 phó bản + Đánh quái) để đổi thứ tự ưu tiên; ghi nhớ thứ tự.
- Hiện tại: thứ tự cố định Liên Trảm, Thiên Quan, Doanh Trại, Phu Tử, Mê Cung (theo chỉ số trong code, `PbArgs` gửi `list=` theo thứ tự này);
  Đánh quái luôn chạy sau cùng, chỉ khi không còn phó bản nào cần làm (`PbWanted`).
- Dự kiến: lưu thứ tự trong `train_<acc>.ini` (mỗi tài khoản); `PbArgs` gửi `list=` theo thứ tự đã kéo; SWF đã chạy theo thứ tự `list=` sẵn
  (phó bản đang làm vẫn làm hết lượt rồi mới xét thứ tự, theo 03-10a).
- **Đã chốt (03-10):**
  1. Đánh quái dù kéo lên trên vẫn nhường mọi phó bản (ngoại lệ duy nhất của thứ tự) — vị trí dòng Đánh quái chỉ để sắp xếp hiển thị;
  2. thứ tự riêng từng tài khoản;
  3. kéo thả bằng chuột, có hiệu ứng báo điểm rơi khi đang kéo (vạch kẻ ngang ở chỗ sẽ thả + dòng đang kéo mờ đi).
- Dự kiến: lưu `order=` (vd `tq,dt,train,lt,pt,mc`) trong `train_<acc>.ini`; đổi tài khoản thì xếp lại các dòng; thả xong gửi `pb_list` nếu đang chạy phó bản.

**Sửa mức % máu của skill hồi máu đã thêm** (Hain báo 03-10, chờ lệnh sửa)
- Hain muốn: trong danh sách hỗ trợ (tab KỸ NĂNG), sửa được mức "máu dưới X%" của skill hồi máu đã thêm, không phải xóa rồi thêm lại.
- Hiện tại: `g_supList` chỉ có thêm (+) / xóa; mức % chỉ nhập lúc thêm (`IDC_EDIT_SUPPCT`, 5–95).
- Dự kiến: nhấp đúp vào cột "Điều kiện" của dòng hồi máu → ô nhập số ngay trên dòng, Enter/rời ô thì lưu (5–95), Esc hủy;
  lưu xong dùng `SkillSetChanged` → áp dụng ngay (cả đang Đánh quái lẫn đang chạy phó bản, theo 03-10d). Dòng buff không có mức %, nhấp đúp không làm gì.

**Mê Cung chỉ mở thứ 3, 5, 7** (Hain báo 03-10, chờ lệnh sửa)
- Hain muốn: xem lịch trên máy, ngày khác thứ 3/5/7 thì không chạy Mê Cung.
- Dự kiến: panel `PbReady` thêm điều kiện ngày (giờ máy, `GetLocalTime`, wDayOfWeek 2/4/6); ngày đóng: dòng Mê Cung hiện "Không mở hôm nay",
  không gửi `mc` trong `list=`; đang trong Mê Cung lúc qua nửa đêm thì vẫn làm nốt lượt. Nên làm dạng bảng ngày mở cho mọi phó bản (mặc định Mê Cung 3/5/7, còn lại mọi ngày).
- Còn hỏi: game đổi ngày lúc 0h hay giờ khác (vd 5h)? Giờ máy có khớp giờ server không?

**Mê Cung thần bí 2 — sóc báu** (Hain báo 03-10, chờ lệnh sửa)
- Hain báo: Mê Cung có thể vào "ải mê cung thần bí 2"; có 1 con **sóc báu chạy quanh map** → tool phải quét vị trí liên tục, đuổi theo và đánh.
  Tick "bỏ qua ải chuột và phòng thần bí" thì bỏ qua cả ải này (đi thẳng ra cổng).
- Hiện tại nhân vật **đứng yên** khi vào ải này. Nguyên nhân trong code: map ải này không có trong danh sách map Mê Cung (`PB_DEF.mc.maps`, chỉ có 20033, 20177–20192)
  → `pbInStep` thấy "không phải map của phó bản đang chạy" và không làm gì (chỉ xử lý khi về Tương Dương).
- Dự kiến:
  - nhận ải này theo map ID (cần Hain cho) và dự phòng: đang chạy Mê Cung mà vào một map phó bản lạ (`isFuben`) → coi là phòng thần bí, log map ID để bổ sung;
  - không bỏ qua: mỗi nhịp tìm lại con sóc theo tên (chứa "sóc"), đuổi theo bằng lệnh đi tới đúng vị trí hiện tại của nó (cập nhật khi nó chạy > 2 ô), trong tầm thì ra chiêu;
    không thấy sóc thì đi tuần quanh map; sóc chết → nhặt đồ rơi rồi ra cổng; chưa có tuyến thì dùng các điểm chia đều bản đồ;
  - bỏ qua (tick): đi thẳng tới cổng ra như phòng thần bí 1 (`mcSecretExit`).
- **Đã chốt (03-10):**
  - tìm sóc: quét **toàn map (99 ô)** mọi quái client đang biết, tên chứa "Sóc" (không phân biệt hoa thường); chỉ khi client chưa thấy con nào mới đi tới giữa map / các điểm chia đều để lộ ra;
    (lưu ý: client có thể chỉ nhận quái trong tầm nhìn — nếu vậy quét 99 ô vẫn không thấy khi sóc ở xa, phải dựa vào điểm dự phòng);
  - đuổi: **nhảy** tới gần sóc (charJump, điều kiện game: thể lực ≥ 20, map cho nhảy, không bị trói; tầm nhảy theo game), nhảy không được thì đi; trong tầm thì ra chiêu;
  - ra khỏi ải: **bước vào cổng** (như phòng thần bí 1);
  - map ID: chưa biết → bản sửa tự log map ID lần đầu vào ải.

**Phu Tử**
- E1. Quái theo thứ tự có thể kẹt vĩnh viễn (giống B1): `run.ptTarget` được gán lại sau khi bị bỏ, và lúc tìm không bỏ qua `_black`. Sửa: bỏ con đó, tìm con cùng tên khác; chỉ còn đúng con đó thì đi tuần một vòng rồi thử lại.
- E2. Giết đúng con theo thứ tự không tính kill, không chờ đồ rơi. Sửa: tính kill + chờ đồ rơi 1 giây.
- (Đính chính: bản 02-10j ghi nhầm "Phong Thần", đúng là Phu Tử — sửa trong ghi chú cập nhật bản tới.)

**Thứ tự chạy phó bản** (Hain báo 03-10) — **[đã sửa 03-10a]**
- SWF nhớ phó bản đang làm (`_pbCur`, giữ cả lúc về thành giữa 2 lượt); `pbNextKey` ưu tiên nó tới khi hết lượt (NPC báo) / bị bỏ qua / bỏ tick.
- Lệnh mới `pb_list` (cùng tham số pb_start + `on=` mọi phó bản đang tick): panel tick/bỏ tick lúc đang chạy chỉ gửi pb_list, không pb_stop/pb_start.
  Phó bản đang làm còn tick mà panel tạm bỏ khỏi `list` (giờ/điều kiện) vẫn được giữ. Bỏ tick đúng phó bản đang làm → thoát (lượt thất bại; đang chết thì sau khi hồi sinh), sự kiện `pb unpick <k>`.
  Bỏ tick hết: panel không pb_stop ngang, SWF tự thoát rồi `pb finish`. SWF cũ trả "không hỗ trợ" → panel làm như trước (pb_stop rồi chạy lại).

Mô tả gốc:
- Ví dụ: đang chạy Doanh Trại, tick thêm Thiên Quan (nằm trên trong danh sách) → bot ra Doanh Trại đi Thiên Quan.
- Hain muốn: làm xong Doanh Trại rồi mới tới Thiên Quan.
- Nguyên nhân trong code:
  - mỗi lần tick/bỏ tick, panel gửi `pb_stop` rồi `pb_start` với danh sách mới;
  - `pbStart` có chạy tiếp lượt đang dở, nhưng hết lượt thì `pbNextKey` chọn lại từ đầu danh sách;
  - đang ở thành giữa 2 lượt thì đi Thiên Quan ngay.
- Đề xuất:
  - nhớ phó bản đang chạy; tick/bỏ tick phó bản khác chỉ cập nhật danh sách (không stop/start);
  - phó bản đang chạy làm hết lượt rồi mới chọn tiếp theo thứ tự;
  - chỉ thoát khi bỏ tick đúng phó bản đang chạy.

**Câu hỏi chờ chốt**
1. Tùy chọn "Bỏ qua đánh Boss ở trạng thái đếm số" có thêm cho Doanh Trại, Liên Trảm, Phu Tử không? Mặc định tick hay không?
2. Nhịp nhảy Doanh Trại: nhảy lúc rảnh, hay giãn 2–3 giây?
3. Mê Cung: gửi 10051 tại cửa rồi mới kết luận cửa sai?
4. Làm B1–B3, C1, D1–D3, E1–E2 và thứ tự phó bản trong cùng một bản?
5. Mê Cung: đã chốt và đã sửa (03-10c).

## 12. Phó bản [đọc mã game; mock]

| Phó bản | id bảng | Map |
|---|---|---|
| Liên Trảm (PB15) | 1 | 20032 (boss ~ (78,56), đi qua (78,65)) |
| Thiên Quan (PB20) | 2 | 20038 → 20050 (13 tầng) |
| Doanh Trại (PB25) | 3 | 20060,20110,20061,20111,…,20069,20119 (`OtherConst.isDoanhTraiMap`) |
| Phu Tử Trận (PB30) | ? | 20175; NPC 1738 (12,14) cho thứ tự: `GameInstance.npcTishiDict[1738]`; khu quái (36,25); boss "Khôi Khôi" |
| Mê Cung Trận (PB40) | ? | 20033, 20177…20191 (16 phòng), phòng thần bí 20192; cửa trái (40,26) / phải (116,28); phòng cuối NPC 1740 (84,64) |

- Vào: NPC **1656** ở Tương Dương (~58,67). `Team_MsgSenderProxy` (com.tgame.moudels.team.model): `send_10711(npc)` mở bảng → nhận **10712**
  `short n; n × [int idPB, int mapTầngĐầu, short đãVào, short tổng, short phóBảnLệnh, short, byte]` → `send_10701([npc, mapTầngĐầu, 1 = thưởng thường, 1 = độ khó "Trung bình" (thấp nhất; 2 = Khó)])`.
- Hoàn thành: nhận **10726** → `send_10723()` (nút nhận thưởng) → **10728** nhận thưởng xong.
- Cổng: bước lên cổng → client gửi 10051 [x,y]. Rời phó bản: client hỏi FAlert trước. Qua ải: server gửi **10052** `[short x, short y, UTF câu hỏi]` → FAlert → Xác nhận → 10053 [x,y].
  Tool bấm nút `FAlert.ok` (tên con trong FAlert) bằng MouseEvent.CLICK. Đi sang map: `mainCharWalk("<map>,-1,-1,0")` (game tự tìm cổng).
- **10300** `[int transId, int destPos, byte isOpen]`: cổng mở/đóng.
- Hồi sinh: bảng `ReLivePanel` trong `POPWindowManager` (modal, tự về thành sau 15s). Nút tại chỗ = BaseEvent "UI_RELIVEPANEL_LOCALE_RELIVE_BTN_CLICK" → 20075 byte 0 (tốn hoa); về thành = "…RETURN…" → byte 1. Hoa hồng: id **1201–1206** (cộng cả 6).
- Nhảy: `MainCharSeachPathManager.charJump(mainChar, Point(x,y), -1, 500, null, false, false)`; điều kiện như Shift+click: `getMapRes(map).allowJump`, `attributeInfo.ppNow >= 20`, không `isSoft`, `FightManager.isMainCharCanMove()`. Game có sẵn ô "Tự nhảy Doanh Trại" (`GameState.autoJumpDoanhTrai`) trong bảng Trợ Chiến.
- Bảng Trợ Chiến của game có tab Phó bản (Liên Trảm/Thiên Quan/Doanh Trại) đang ghi "Sắp mở" — người dùng không muốn dùng.
- Lệnh pipe: `pb_list` (như pb_start, thêm `on=`; đang chạy thì chỉ cập nhật danh sách) | `pb_start list=lt,tq,dt <k>_runs= <k>_rev= tq_minr= dt_jump= <k>_route=x:y;… done=lt:n,…`, `pb_stop`. Status thêm `pb pbphase pbfloor pbdeaths roses`.
  Sự kiện `! train pb start|enter|run|floor|revive|confirm|reward|exit|end <k> ok|fail …|skip <k> lý do|finish|stop`. vlcmhost gửi `! hostclose` khi đóng theo ý người dùng.

- Phu Tử: hỏi NPC `MainChar_MsgSenderProxy.send_10129(1738)`; tắt ám khí `Fight_MsgSenderProxy.send_52005(0/1)` (trạng thái `mainCharData.anqiInfo.isOpen`);
  cung `BowArrow_MsgSenderProxy.send_53043(0/1)` (trạng thái bật chỉ có khi mở bảng cung → panel có ô "bật lại cung"); thú chiến `Mount_MsgSenderProxy.send_50039(id)` cất / `send_50037(id)` thả
  (`mainCharData.mountInfo.currentFightMountID`). Tên quái: `getHeadFaceNickName()` → `headFace.nickName` → `data.name` → `data.res.name`. Boss nhảy khi mang buff `res.type == 26`.
- Ải chuột (không biết map → nhận theo tên quái có chữ "chuột", mọi phó bản): chỉ đánh chuột, bán kính 200; chuột chết → chờ gói 11162 của đúng con đó (`_lastDropMob[mobId]`, tối đa 1.5s) → nhặt hết đồng (trong phó bản luôn nhặt đồng) → mới đánh con gần nhất kế tiếp. Không thấy chuột: đi tuần tuyến 1 vòng, yên PB_QUIET → "hết chuột và đồng", trả về đánh bình thường.
  Tuyến: Doanh Trại dùng `dt_route`; Mê Cung tầng 15 (phòng 20190) dùng `mc_r15` = MeCung15.xml (mặc định 10 điểm sẵn); phòng thần bí 20192 dùng `mc_sroute` = MeCungThanBi.xml.
- Liên trảm [đọc mã game]: **12001** `[short số chuỗi]` (0 = đứt) → `Fight_LianZhanProxy.lianzhan`; **12002** `[byte id mốc]` → `mainCharData.lianzhanInfo.id` → thời gian giữ chuỗi `lianzhanInfo.time` (ms, từ combo XML `@time`; chuỗi càng cao càng ngắn). `lianzhanInfo.count` = chuỗi hiện tại.
  Buff liên trảm: id **60001–60007** (`OtherConst.isLianZhanBuff`), trong `me().data.buffInfo.buffArr`; thời gian còn = `CDFaceManager.getLosttime(buff.getID())`. Mức (vd 400) tool lấy số đầu tiên trong `res.name` (rồi `res.desc_short`, `miaoShu`) — CHƯA kiểm trên game thật, log "lz buff liên trảm …" để đối chiếu.
- Liên Trảm (tool): đi theo tuyến không chờ (lặp lệnh đi 0,5s, tới điểm ≤ 3 ô là đi tiếp); dưới mốc X dọn lính trong 20 ô; từ mốc X dụ quái: đường đi không đánh, tới điểm giết `kpp` con (ưu tiên ít máu, ≤ 6 ô, tối đa 8s) rồi đi; còn < 40% thời gian giữ chuỗi → "cứu chuỗi": đánh con gần nhất ≤ 20 ô, SKILL_GAP 0, HIT 0,4s, bỏ buff/nhặt.
  Lúc tích chuỗi không đánh boss (`res.type == 3`, giết boss là hết phó bản). Đạt Y (không "tiếp tục tới khi đứt") hoặc đứt chuỗi sau khi đạt Y, hoặc hết vòng không thấy lính → boss: thấy boss thì đi thẳng tới, chưa thấy thì (78,65) → (78,56). Bỏ qua boss → ra phó bản, tính hoàn thành.
- pb_start thêm: `<k>_lz=lt:A:B` (dưới A hoặc còn < B phút — LT/MC) | `ge:A:B` (từ A và còn ≥ B phút — TQ/DT/PT); `<k>_sf=N` (qua ải N thì ra, tính hoàn thành — TQ/DT); `<k>_nomob=N` (N phút không thấy quái thì ra, thất bại — TQ/DT/PT);
  `lt_y` (0 = tắt) `lt_cont` `lt_lure` `lt_x` `lt_kpp` `lt_pick` (0 không nhặt / 1 vừa tích vừa nhặt) `lt_skipboss`. Sự kiện mới `pb wait <k> lý do` (chưa đủ điều kiện liên trảm; panel tạm bỏ 60s). Status thêm `lz lzb lzbmin lzmax`; pbfloor của LT = "x<chuỗi>".
- Panel: khung cài đặt riêng từng phó bản (nút "..." ở dòng phó bản, hiện đè vùng tab bên phải, nút X đóng). Khung giờ + điều kiện liên trảm panel tự xét trước khi gửi danh sách. Client tắt bất thường: tự mở lại không giới hạn (8s; ≥ 3 lần / 10 phút thì 60s).
- [02-10, đọc mã game] Cổng: **10300** `[int mapID, int destPos, byte isOpen]` — game bỏ qua nếu mapID ≠ map đang đứng (`Scene_SceneProxy.receivedTransIsOpened`), rồi bật `TransOutRes.opened` (cổng = scene char type 7, `data.res` có `mapID, x, y, opened, enable`). Tool chỉ sang tầng khi cổng tầng kế opened (hoặc 10300 đúng map) + không mục tiêu 1s + hết túi chưa cấm.
  Bước lên cổng: game gửi `Engine_MsgSenderProxy.send_10051([tile_x, tile_y])` (Engine_SceneWalkCommand). Hỏi qua ải 10052 `[x,y,msg]` → nút OK gọi `send_10053([x,y])` (Engine_MsgReceivedProxy). Tool: bấm FAlert; 3s chưa sang thì gửi 10053 [x,y] của 10052; đứng sát cổng 2s mà chưa có gì thì gửi 10051 tọa độ ô cổng.
- [02-10] Treo máy của game: `mainCharData.afkInfo2` (AfkInfo2, `afkRangeCount` — ô nhập của game giới hạn ≤ 99, `isStart`). Lưu: `Afk_MsgSenderProxy.send_50581()` (gửi cả afkInfo2) → server 50582 [byte] → `Afk_Mediator.saveSettingsOK`: byte 0 thì game tự `send_50583(1)`. Dừng: `send_50583(0)`. Tool (Thiên Quan, ô tick): hết vòng tuần không thấy quái mà cổng chưa mở → đặt phạm vi 99, lưu → treo; cổng mở / hoàn thành → trả phạm vi cũ, lưu, dừng; quá 3 phút → như trên rồi thoát (thất bại).
- [02-10] Tầm skill: game so pixel `Point.distance(mainChar.piexl, target.piexl) <= res.distance * 25`. `Fight_proxy.useSkill`: target 1/2/11 (và 3/4 có user_scope 2 + aoe_type 1) tung tại chỗ (`attackByNow`, không tự chạy tới); còn lại `attackByTarget` → HIT_CHAR. Tool: skill ngoài tầm thì `mainCharWalk("map,x,y,<tầm px>")` (như `attackByPointOK`), 3s không lại gần được thì bỏ con đó 10s.
- [02-10] Nhặt trong phó bản (`pbPickStep`): nhặt trước đánh sau; luôn nhặt hoa hồng (1201–1206) + đồng; ưu tiên hoa > đồng > luật > gần; túi đầy chỉ hoa + đồng; ≤ 1 ô (Manhattan) chỉ gửi PICK_UP (≥ 200ms); 1s không nhích / bị `isFixed`/`isSleep` thì cho đánh quái 1s; 6s chưa được → cấm 10s + bước lệch, lần 3 cấm hẳn trong lượt; server báo "không thể nhặt"/túi đầy → cấm hẳn. Log "pick bỏ …" kèm trạng thái.
- [02-10] Số lượt: tool gửi `pb count <k> <đã vào> <tối đa>` mỗi lần đọc 10712; panel bỏ ô số lượt, đi tới khi hết lượt. Chờ 3s sau khi nhận 10712 mới gửi 10701. pb_start thêm `<k>_range` (mặc định 99), `tq_afk`.
- [02-10b] Nhặt như game (`MainChar_MainCharProxy`, bấm vào túi type 11): `mainCharWalk("map,x,y,0", false, MoveCallBack)` → tới nơi gửi PICK_UP; tool không gửi PICK_UP khi `onMoveUnable`. Dự phòng: pipe `KEY_SPACE` (= phím Space; `PickUp_AutoPickUpCommand` → `send_11163(-1)` nhặt quanh mình, giới hạn 400ms; bỏ qua nếu `GameState.SPACE_TO_SKILL` hoặc `afkInfo2.isStart`).
  Chống đơ: `goPoint` (2,5s không lại gần 1 ô → điểm hỏng, nhớ theo map), `walkWatch` (status walk 2s không đổi ô → `MainCharSeachPathManager.clear()` + `mainChar.stopMove()` + bước lệch), quái không tới được 2 lần → bỏ 10 phút.
- [02-10d] Đi lại gần quái: khoảng cách dừng = 60% tầm (tối thiểu 40px); ≤ 1 ô coi là trong tầm; điểm đến < ~1 ô hoặc lệnh đi 0,5s không nhích → "coi như trong tầm" con đó 3s (`_forceId`).
- [02-10e] `moveTo`: mọi lệnh đi chung một chỗ, cùng đích (±1 ô) không gửi lại < 1,5s (đang đi/đã nhích: < 4s); không đi khi isFixed/isSleep; không buff khi đang đi (ra chiêu → game `stopMove`); log `kẹt đi:` khi 1s không nhích.
- [02-10f] TARGET_GIVEUP 20s chỉ khi máu mục tiêu không giảm; boss (`res.type == 3`) không bao giờ bị bỏ (cả luật 12 ô, luật không lại gần được). Sau khi giết: chờ `_lastDropMob[id]` (quái 1s; boss 2s, +1s sau túi cuối) rồi mới chọn mục tiêu. Boss bất tử/bảo hộ = `BuffInfo.isProtectOrInvincibleBuff` (id 80001/80004/4001/4004/4011 hoặc res.type 172) → tùy chọn `tq_bosscount` (mặc định 1).
- [02-10g] pb_start nhận thêm cài đặt đánh giống train_start (`skillmode g1..g4 heal buff supmp keys pick picklist`, panel `FightArgs`); SWF `applyFightCfg` dùng chung, log `pb kỹ năng: …`. Trước đó phó bản không có hồi máu/buff/luật nhặt nếu chưa chạy Đánh quái.
- [02-10h] Bộ kỹ năng riêng từng phó bản: panel `pbSet[i]` (-1 = giống Đánh quái) → pb_start `<k>_set <k>_g1..g4 <k>_heal <k>_buff <k>_supmp`; SWF áp dụng ở `pbBeginRun` (log `pb <k> dùng nhóm kỹ năng N`).
- [02-10i, đọc mã game] Tung skill lên bản thân: `Fight_proxy.useSkill` → target 1/2/11 `attackByNow` → `attack()` gọi `mainChar.stopMove(true)` rồi 10083. Skill chọn mục tiêu (3/4 scope 1, 6, 9, 10) nhắm chính mình: `attackByTarget` → HIT_CHAR → `attackChar` → `send_attack` chỉ tung lên người chơi (type 1) khi pkMode ≠ hòa bình hoặc giữ Shift/Ctrl → hòa bình thì KHÔNG tung.
  Tool: buff lại sau ≥ thời gian tác dụng (`buffres.duration`, không có thì 30s); không buff khi đang đi; heal khi đi cách ≥ 3s (máu < nửa mốc thì ngay); 2 lần không vào hồi chiêu → tạm bỏ 2 phút. `walkCmd`: mọi lệnh đi ≥ 0,7s/lệnh, log `spam đi` (≥ 4 lệnh/5s, nhích < 2 ô). Hoa: đếm lại 3 lần cách 3s trước khi bỏ qua.
- Mê Cung: đi lên ô cửa → game tự dịch chuyển; phòng cuối: `Team_MsgSenderProxy.send_52059()` (chức năng NPC 24) nhận quà. Đóng hộp thoại NPC: pipe "HIDE_NPC_DIALOG_PLANE".
- [02-10j, đọc mã game] Game tái sử dụng đối tượng nhân vật: `Scene.removeCharacter` → `SceneCharacter.recycleSceneCharacter` (ScenePool), `createSceneCharacter` lấy lại từ pool. Ngựa chiến (loại 3, `receivedMountEnterScene`: rời rồi tạo lại) hay lấy đúng đối tượng của con quái vừa chết → tham chiếu cũ của tool thành ngựa chiến (usable, không chết, đứng cạnh) → khóa + HIT_CHAR lên ngựa → `attackChar.send_attack` gửi 10083 loại 3 → server báo "mục tiêu không tồn tại" (receivedAttackFailure 0). Sửa: `_target` là getter/setter nhớ id + loại 2 (`mobRefOk`); `guardRefs` đầu mỗi nhịp bỏ mục tiêu đổi loại/id (tính là đã chết: kill + chờ đồ rơi), gỡ `lockOnChar` loại 3/4/8 hoặc không usable, hủy lệnh đuổi theo (`MainCharSeachPathManager.clear` + `stopMove`). Áp cho chuột, PT theo thứ tự, boss quay lại (`run.mouseTargetId/ptTargetId/bossId`). Hỗ trợ xong khôi phục khóa qua `safeLock`. Mock: cờ `recycle`.
