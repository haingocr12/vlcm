package {
import flash.events.Event;
import flash.events.TimerEvent;
import flash.utils.ByteArray;
import flash.utils.Timer;
import flash.utils.getQualifiedClassName;
import flash.utils.getTimer;

/**
 * VlcmUtil: các tiện ích dùng chung (thuốc, sửa đồ, bán/hủy, về thành, truyền tống VIP, thống kê đồng).
 *
 * Mọi thao tác đi qua hàm gửi có sẵn của client, giống người chơi bấm:
 *   dùng vật phẩm     PipeManager "ITEM_USE" [ItemFace, 1]      (như nhấp đúp ô trong túi) — dự phòng send_11201
 *   hủy vật phẩm      Item_MsgSenderProxy.send_11181([ô, 0])    (đúng hàm Item_ItemProxy.removeItem)
 *   bán cho NPC       Item_MsgSenderProxy.send_10883([npc, id, số lượng, ô])   (nút Bán của cửa hàng NPC)
 *   sửa đồ            send_50221(npc) rồi send_50223([1 thường | 2 đặc biệt, npc])  (bảng sửa đồ)
 *   đi tới NPC        MainCharSeachPathManager.mainCharWalk(idNPC)   (như bấm tên NPC)
 *   truyền tống       MainUI_MsgSendProxy.send_10133({id: map})  — CHỈ khi attributeInfo.isVIP == 1 (miễn phí)
 *
 * Bán/hủy có nhiều lớp an toàn (xem SAFETY bên dưới); mặc định không đụng vào món nào.
 */
public class VlcmUtil {

    // NPC ở Tương Dương (Redirect.xml)
    public static const TOWN_MAP:int = 20002;
    public static const NPC_VUKHI:int = 1638;
    public static const NPC_TAPHOA:int = 1739;
    private static const VIP_BUFFS:Array = [1300, 80000];

    // SAFETY — giới hạn cứng
    private static const MAX_DESTROY:int = 60;          // tối đa món hủy mỗi phiên
    private static const MAX_SELL:int = 120;            // tối đa món bán mỗi phiên
    private static const ACTION_GAP:int = 500;          // tối thiểu 0.5s giữa 2 thao tác
    private static const ACTION_WAIT:int = 3000;        // chờ server xác nhận
    private static const ANOMALY_COUNT:int = 10;        // khớp > 10 món cùng lúc => bất thường
    private static const ANOMALY_RATIO:Number = 0.3;    // khớp > 30% túi (khi túi >= 10 món) => bất thường
    private static const MAX_FAILS:int = 3;             // 3 lần liên tiếp không xác nhận được => dừng

    private static const TICK_MS:int = 200;
    private static const STARTUP_UI_FROM:int = 3000;    // đóng cửa sổ tính năng từ giây 3 ...
    private static const STARTUP_UI_TO:int = 20000;     // ... tới giây 20 sau khi vào game (như bấm Esc)
    private static const POTION_GAP:int = 1000;
    private static const PLAN_EVERY:int = 5000;
    private static const TELE_WAIT:int = 8000;
    private static const TELE_RETRY:int = 300000;
    private static const TRIP_TIMEOUT:int = 240000;
    private static const TRIP_COOLDOWN:int = 600000;

    private var _c:Object;
    private var _emit:Function;
    private var _timer:Timer;
    private var _sender:Object;       // Item_MsgSenderProxy (tự tạo, chỉ dùng các hàm send_)
    private var _uiSender:Object;     // MainUI_MsgSendProxy

    // ---- cấu hình (util_set)
    private var _potMode:String = "keys";      // keys | items
    private var _hpKey:int = -1, _mpKey:int = -1;
    private var _hpPct:int = 40, _mpPct:int = 20;
    private var _hpIds:Array = [], _mpIds:Array = [];
    private var _repair:Boolean = false;
    private var _repairType:int = 1;
    private var _bh:Boolean = false;
    private var _rules:Array = [];
    private var _protect:Array = [];
    private var _floorQ:int = 4;               // không đụng phẩm chất >= (4 = Tím). 99 = tắt
    private var _floorS:int = 1;               // không đụng trang bị cường hóa >= +N. 99 = tắt
    private var _freeE:int = 5, _freeI:int = 5;
    private var _tele:Boolean = true;
    private var _vllAuto:Boolean = false;

    // ---- trạng thái
    private var _lastHpPot:int, _lastMpPot:int, _lastPlan:int, _lastVll:int = -TRIP_COOLDOWN;
    private var _paused:Boolean = false;
    // đóng cửa sổ: bảng truyền tống sau khi đã truyền tống; cửa sổ tính năng lúc mới vào game
    private var _portalCloseUntil:int = 0;
    private var _lastUiClose:int = 0;
    private var _closeUi:Boolean = true;          // util_set closeui=0|1
    private var _enterAt:int = -1;                // lúc thấy nhân vật trong game (lần vào game này)
    private var _uiClosed:Object = {};            // tên lớp cửa sổ đã đóng (ghi log một lần)         // panel bỏ tick tài khoản: tạm dừng thuốc / VLL / bán-hủy
    private var _bhStopped:String = "";         // khác rỗng = đã dừng khẩn cấp (lý do)
    private var _destroyed:int, _sold:int, _fails:int;
    private var _act:Object;                    // thao tác đang chờ xác nhận
    private var _lastActEnd:int;
    private var _skipFp:Object = {};            // dấu vân tay món không bán/hủy được: bỏ qua
    private var _dryLogged:Object = {};
    private var _startCopper:Number = -1;
    private var _statsSince:int;
    private var _mPick:Number = 0, _mSold:Number = 0, _mRep:Number = 0;
    private var _expect:Array = [];             // đồng chờ xác nhận {amt, base, until, name}
    // về thành
    private var _trip:Object;
    private var _tripFailAt:int = -TRIP_COOLDOWN;
    private var _teleState:Object;              // {map, at}
    private var _teleFailAt:int = -TELE_RETRY;
    // thông báo server (22222): dùng để biết lệnh nhặt / truyền tống bị từ chối
    private var _notice:Object = { at: -100000, msg: "", used: true };
    private var _noticeSeen:Object = {};
    private var _hooked:Boolean = false;
    private var _mLost:int;
    // cài đặt trong game (SETTING_DATA như phím tắt / bảng Cài đặt của game)
    private var _gs:Object = {};                // idx -> bool mong muốn
    private var _gsApplied:Object = {};         // idx -> bool đã gửi
    private var _hpfx:Boolean = false, _hpfxApplied:Boolean = false;
    private var _gsMap:int = -1;
    // hiển thị / hiệu năng
    private var _render:String = "show";        // show | hide1 | hide2
    private var _saved:Object;                  // trạng thái vẽ trước khi ẩn
    private var _perf:Array = [newBucket(), newBucket(), newBucket()];
    private var _frames:int;
    private var _lastPerf:int, _lastUtilTick:int;
    private var _walkX:Number = NaN, _walkY:Number = NaN;
    private var _stageHooked:Boolean = false;

    public function VlcmUtil(classes:Object, emit:Function) {
        _c = classes;
        _emit = emit;
        try { if (_c.Item_MsgSenderProxy) { var C1:Class = _c.Item_MsgSenderProxy as Class; _sender = new C1(); } } catch (e:Error) { }
        try { if (_c.MainUI_MsgSendProxy) { var C2:Class = _c.MainUI_MsgSendProxy as Class; _uiSender = new C2(); } } catch (e:Error) { }
        _statsSince = getTimer();
        _lastPerf = getTimer();
        _timer = new Timer(TICK_MS);
        _timer.addEventListener(TimerEvent.TIMER, onTick);
        _timer.start();
    }

    // ------------------------------------------------------------------ lệnh

    /** null = không phải lệnh của util */
    public function command(cmd:String, args:String):String {
        switch (cmd) {
            case "util_set":  return set(parseArgs(args));
            case "bag_dump":  return bagDump();
            case "bh_stop":   emergency("dừng theo lệnh"); return "ok";
            case "bh_reset":
                _bhStopped = ""; _destroyed = 0; _sold = 0; _fails = 0; _skipFp = {}; _dryLogged = {};
                note("bh", "info bật lại bán/hủy, đặt lại bộ đếm phiên");
                return "ok";
            case "stats_reset":
                _mPick = 0; _mSold = 0; _mRep = 0; _mLost = 0; _statsSince = getTimer(); return "ok";
            case "render":    return render(String(parseArgs(args).mode));
            case "util_pause":
                _paused = parseArgs(args).on == "1";
                note("util", "info " + (_paused ? "tạm dừng các chức năng tiện ích" : "bật lại các chức năng tiện ích"));
                return "ok pause=" + (_paused ? 1 : 0);
            case "perf_reset": _perf = [newBucket(), newBucket(), newBucket()]; return "ok";
        }
        return null;
    }

    private function set(kv:Object):String {
        if (kv.pot != undefined) _potMode = String(kv.pot);
        if (kv.hpkey != undefined) _hpKey = int(kv.hpkey);
        if (kv.mpkey != undefined) _mpKey = int(kv.mpkey);
        if (kv.hp != undefined) _hpPct = int(kv.hp);
        if (kv.mp != undefined) _mpPct = int(kv.mp);
        if (kv.hpids != undefined) _hpIds = intList(kv.hpids);
        if (kv.mpids != undefined) _mpIds = intList(kv.mpids);
        if (kv.repair != undefined) _repair = kv.repair == "1";
        if (kv.closeui != undefined) _closeUi = kv.closeui == "1";
        if (kv.rtype != undefined) _repairType = int(kv.rtype) == 2 ? 2 : 1;
        if (kv.bh != undefined) _bh = kv.bh == "1";
        if (kv.floorq != undefined) _floorQ = int(kv.floorq);
        if (kv.floors != undefined) _floorS = int(kv.floors);
        if (kv.freee != undefined) _freeE = int(kv.freee);
        if (kv.freei != undefined) _freeI = int(kv.freei);
        if (kv.tele != undefined) _tele = kv.tele == "1";
        if (kv.vllauto != undefined) _vllAuto = kv.vllauto == "1";
        if (kv.gs != undefined) {                    // "0:1,2:1,..."
            _gs = {};
            for each (var gp:String in String(kv.gs).split(",")) {
                var gc:int = gp.indexOf(":");
                if (gc > 0) _gs[int(gp.substr(0, gc))] = gp.substr(gc + 1) == "1";
            }
            _gsMap = -1;                             // áp dụng lại ở tick sau
        }
        if (kv.hpfx != undefined) { _hpfx = kv.hpfx == "1"; _gsMap = -1; }
        if (kv.protect != undefined) {
            _protect = [];
            for each (var p:String in decodeURIComponent(String(kv.protect)).split(",")) {
                p = trim(p).toLowerCase(); if (p) _protect.push(p);
            }
        }
        if (kv.rules != undefined) {
            var raw:String = decodeURIComponent(String(kv.rules));
            _rules = [];
            var bad:int = 0;
            var lines:Array = raw ? raw.split("\n") : [];
            for (var i:int = 0; i < lines.length; i++) {
                var r:Object = parseRule(lines[i]);
                if (r) { r.idx = i + 1; _rules.push(r); } else if (trim(lines[i])) bad++;
            }
            if (bad) note("bh", "warn " + bad + " quy tắc bán/hủy không hợp lệ (không có điều kiện) đã bị bỏ qua");
            _dryLogged = {}; _skipFp = {};
        }
        return "ok pot=" + _potMode + " repair=" + (_repair ? _repairType : 0) + " bh=" + (_bh ? 1 : 0)
            + " rules=" + _rules.length + " floorq=" + _floorQ + " floors=" + _floorS + " tele=" + (_tele ? 1 : 0);
    }

    // "act=destroy;real=0;kind=equip;q=1;s=-1;g=-1;p=;pos=;lock=any;names=A|B"
    private static function parseRule(line:String):Object {
        line = trim(line);
        if (!line) return null;
        var r:Object = { act: "", real: false, kind: "", q: -1, s: -1, g: -1, p: [], pos: [], lock: "any", names: [] };
        for each (var part:String in line.split(";")) {
            var e:int = part.indexOf("=");
            if (e <= 0) continue;
            var k:String = trim(part.substr(0, e)), v:String = trim(part.substr(e + 1));
            if (k == "act") r.act = v;
            else if (k == "real") r.real = v == "1";
            else if (k == "kind") r.kind = v;
            else if (k == "q") r.q = v == "" ? -1 : int(v);
            else if (k == "s") r.s = v == "" ? -1 : int(v);
            else if (k == "lv") r.g = v == "" ? -1 : int(v);         // cấp nhân vật cần để mặc (limit_grade)
            else if (k == "p") r.p = intList(v);
            else if (k == "pos") r.pos = intList(v);
            else if (k == "lock") r.lock = v;
            else if (k == "names") {
                for each (var n:String in v.split("|")) { n = trim(n).toLowerCase(); if (n) r.names.push(n); }
            }
        }
        if (r.act != "sell" && r.act != "destroy") return null;
        if (r.kind == "equip") {
            // SAFETY 1: quy tắc trang bị phải có ít nhất một điều kiện giới hạn
            if (r.q < 0 && r.s < 0 && r.g < 0 && r.p.length == 0 && r.pos.length == 0 && r.lock == "any") return null;
        } else if (r.kind == "item") {
            if (r.names.length == 0) return null;          // SAFETY 1: vật phẩm phải có tên
        } else return null;
        return r;
    }

    // ------------------------------------------------------------------ trạng thái cho panel

    public function statusPart():String {
        var s:String = "";
        try {
            var a:Object = gi().mainCharData.attributeInfo;
            var vip:Boolean = int(a.isVIP) == 1;
            s += " vip=" + (vip ? 1 : 0);
            if (vip) { var ex:Number = vipExpire(); if (ex > 0) s += " vipexp=" + int(ex / 1000); }
            var cp:Number = copper();
            s += " copper=" + cp + " free=" + freeSlots() + " dur=" + minDurPct();
        } catch (e:Error) { }
        s += " mpick=" + _mPick + " msold=" + _mSold + " mrep=" + _mRep + " mlost=" + _mLost + " since=" + int((getTimer() - _statsSince) / 1000);
        s += " render=" + _render + " perf=" + _perf.map(function(b:Object, i:int, a:Array):String {
            return [b.ms, b.frames, b.fpsSet, b.lagSum, b.lagN, b.lagMax, b.skills, b.kills, b.pickSum, b.pickN, int(b.walkT * 10), b.walkMs].join(",");
        }).join("/");
        s += " upause=" + (_paused ? 1 : 0);
        s += " bh=" + (!_bh ? "off" : _bhStopped ? "stop" : "on") + " bhn=" + _destroyed + "/" + _sold;
        if (_trip) s += " trip=" + _trip.phase;
        return s;
    }

    /** thời điểm hết VIP (ms, giờ máy), 0 nếu không biết */
    private function vipExpire():Number {
        var bi:Object = null;
        try { bi = gi().mainCharData.buffInfo; } catch (e:Error) { }
        if (!bi) try { bi = gi().mainChar.data.buffInfo; } catch (e2:Error) { }
        if (!bi) return 0;
        for each (var id:int in VIP_BUFFS) {
            var b:Object = null;
            try { b = bi.getBuff(id); } catch (e3:Error) { }
            if (!b) continue;
            var t:String = b.vipTime ? String(b.vipTime) : "";
            // "d-m-yyyy  h:mm"
            var m:Array = t.match(/(\d+)-(\d+)-(\d+)\s+(\d+):(\d+)/);
            if (m) return new Date(int(m[3]), int(m[2]) - 1, int(m[1]), int(m[4]), int(m[5])).getTime();
            try { if (b.leavingCoolingtime > 0) return new Date().getTime() + Number(b.leavingCoolingtime) * 1000; } catch (e4:Error) { }
        }
        return 0;
    }

    public function isVip():Boolean {
        try { return int(gi().mainCharData.attributeInfo.isVIP) == 1; } catch (e:Error) { }
        return false;
    }

    // ------------------------------------------------------------------ vòng lặp riêng (chạy cả khi không train)

    private function onTick(e:TimerEvent):void {
        var now:int = getTimer();
        try {
            perfUtil(now);
            if (me() == null) { _enterAt = -1; return; }          // chưa có nhân vật / đăng nhập lại: lần vào game mới
            if (curMap() < 0) return;                            // đang chuyển map: không tính là vào game lại
            if (_enterAt < 0) { _enterAt = now; _uiClosed = {}; }
            if (_portalCloseUntil > now) closePortalPanel();
            if (_closeUi && now - _enterAt >= STARTUP_UI_FROM && now - _enterAt <= STARTUP_UI_TO && now - _lastUiClose >= 1000) {
                _lastUiClose = now;
                closeStartupPanels();
            }
            if (!_hooked) hookNet();
            if (_teleState && (curMap() == _teleState.map || now - _teleState.at > 20000)) _teleState = null;   // đã tới / quá hạn
            if (_gsMap != curMap()) applyGameSettings();
            if (_startCopper < 0) _startCopper = copper();
            checkExpect(now);
            if (_act) { checkAction(now); return; }             // đang chờ xác nhận bán/hủy: không làm gì khác
            if (_paused || isDead(me())) return;
            potions(now);
            vllAuto(now);
            if (!_trip && now - _lastPlan >= PLAN_EVERY) { _lastPlan = now; destroyPass(now); }
        } catch (err:Error) {
            if (_act || _trip) emergency("lỗi " + err.message);
            else note("util", "error " + err.message);
        }
    }

    // ------------------------------------------------------------------ thuốc

    private function potions(now:int):void {
        var a:Object = gi().mainCharData.attributeInfo;
        if (now - _lastHpPot >= POTION_GAP && a.hpMax > 0 && a.hpNow * 100 < a.hpMax * _hpPct) {
            if (usePotion(_potMode == "items" ? _hpIds : null, _hpKey)) _lastHpPot = now;
        }
        if (now - _lastMpPot >= POTION_GAP && a.mpMax > 0 && a.mpNow * 100 < a.mpMax * _mpPct) {
            if (usePotion(_potMode == "items" ? _mpIds : null, _mpKey)) _lastMpPot = now;
        }
    }

    private function usePotion(ids:Array, key:int):Boolean {
        if (ids == null) {
            if (key < 0) return false;
            var slot:Object = null;
            try { slot = _c.ExchangePositionManager.checkItemShortIndex(key); } catch (e:Error) { }
            if (slot != null && cooling(slot)) return false;
            pipe("KEY_" + key);
            return true;
        }
        if (ids.length == 0) return false;
        // ngoài phó bản: loại nhỏ trước; trong phó bản: loại lớn trước (danh sách gửi lên theo thứ tự nhỏ -> lớn)
        var order:Array = isFuben(curMap()) ? ids.concat().reverse() : ids;
        for each (var id:int in order) {
            var g:Object = bagGoodsById(id);
            if (g == null) continue;
            if (cooling(g)) return false;                        // thuốc chung hồi chiêu
            useItem(g);
            return true;
        }
        return false;
    }

    private function bagGoodsById(id:int):Object {
        for each (var g:Object in bag()) if (g && int(g.id) == id && g.count > 0) return g;
        return null;
    }

    private function useItem(g:Object):void {
        try {
            if (_c.ItemFace) { var IF:Class = _c.ItemFace as Class; pipe("ITEM_USE", [new IF(g), 1]); return; }
        } catch (e:Error) { }
        if (_sender) _sender.send_11201([g.id, g.position]);
    }

    private function vllAuto(now:int):void {
        if (!_vllAuto || isVip() || now - _lastVll < TRIP_COOLDOWN) return;
        for each (var g:Object in bag()) {
            if (g && lname(g).indexOf("võ lâm lệnh") >= 0) {
                _lastVll = now;
                useItem(g);
                note("util", "info hết VIP: dùng " + gname(g));
                return;
            }
        }
    }

    // ------------------------------------------------------------------ đồng nhặt

    public function copper():Number {
        try { return Number(gi().mainCharData.moneyInfo.copper); } catch (e:Error) { }
        return 0;
    }

    /** train gọi khi túi tiền biến mất: chỉ tính khi số đồng thật sự tăng */
    public function expectMoney(amt:int, base:Number, name:String):void {
        _expect.push({ amt: amt, base: base, until: getTimer() + 3000, name: name });
    }

    private function checkExpect(now:int):void {
        if (_expect.length == 0) return;
        var cp:Number = copper();
        var keep:Array = [];
        for each (var x:Object in _expect) {
            if (cp >= x.base + x.amt) { _mPick += x.amt; note("train", "pick ok " + x.name); }
            else if (now > x.until) _mLost++;                 // không ghi log (tránh loãng), chỉ đếm
            else keep.push(x);
        }
        _expect = keep;
    }

    // ------------------------------------------------------------------ bán / hủy

    private function bag():Array {
        try { return gi().mainCharData.goodsInfo.goodsBagArr as Array || []; } catch (e:Error) { }
        return [];
    }

    public function freeSlots():int {
        try { return int(gi().mainCharData.attributeInfo.bagCount) * 30 - bag().length; } catch (e:Error) { }
        return 999;
    }

    private static function gname(g:Object):String {
        try { return String(g.res.name); } catch (e:Error) { }
        return "#" + g.id;
    }
    private static function lname(g:Object):String { return trim(gname(g)).toLowerCase(); }

    private static function fp(g:Object):String {
        return [g.id, g.position, g.quality, g.strengthen_grade, g.bind, g.count, gname(g)].join("|");
    }

    /** SAFETY 3: lý do không được đụng tới (null = được) */
    private function protectedReason(g:Object):String {
        var kind:int = -1;
        try { kind = int(g.res.kind); } catch (e:Error) { }
        // Game tự chặn: bán/hủy món có cờ này thì client chỉ hiện bảng nhắc (DestroyTiShiCanvas), không gửi lệnh.
        if (g.isShowDestroyPanel) return "game không cho bán/hủy";
        if (int(g.quality) >= _floorQ) return "phẩm chất cao";
        if (kind == 2 && int(g.strengthen_grade) >= _floorS) return "cường hóa +" + g.strengthen_grade;
        var n:String = lname(g);
        if (n.indexOf("võ lâm lệnh") >= 0) return "Võ Lâm Lệnh";
        if (_hpIds.indexOf(int(g.id)) >= 0 || _mpIds.indexOf(int(g.id)) >= 0) return "thuốc đang dùng";
        for each (var p:String in _protect) if (n.indexOf(p) >= 0) return "danh sách bảo vệ";
        try {
            for each (var q:Object in gi().mainCharData.goodsInfo.goodsOfQuickBar)
                if (q && (q == g || int(q.id) == int(g.id))) return "đang gắn phím tắt";
        } catch (e2:Error) { }
        return null;
    }

    private function ruleMatch(r:Object, g:Object):Boolean {
        var res:Object = g.res;
        if (res == null) return false;
        if (r.kind == "item") return int(res.kind) != 2 && r.names.indexOf(lname(g)) >= 0;   // SAFETY 2: đúng nguyên tên
        if (int(res.kind) != 2) return false;
        if (r.q >= 0 && int(g.quality) > r.q) return false;
        if (r.s >= 0 && int(g.strengthen_grade) > r.s) return false;
        if (r.g >= 0 && int(res.limit_grade) > r.g) return false;
        if (r.p.length && r.p.indexOf(int(res.popsinger)) < 0) return false;
        if (r.pos.length && r.pos.indexOf(int(res.position)) < 0) return false;
        if (r.lock == "locked" && int(g.bind) != 1) return false;
        if (r.lock == "unlocked" && int(g.bind) == 1) return false;
        return true;
    }

    /** quy tắc đầu tiên khớp (null nếu không khớp hoặc món được bảo vệ) */
    private function matchOf(g:Object):Object {
        if (!g || protectedReason(g) != null) return null;
        for each (var r:Object in _rules) if (ruleMatch(r, g)) return r;
        return null;
    }

    /** danh sách {g, r} theo hành động; real = chỉ quy tắc làm thật */
    private function plan(act:String, real:Boolean):Array {
        var out:Array = [];
        for each (var g:Object in bag()) {
            var r:Object = matchOf(g);
            if (r && r.act == act && r.real == real && !_skipFp[fp(g)]) out.push({ g: g, r: r });
        }
        return out;
    }

    /** SAFETY 4: nếu số món khớp bất thường thì dừng hẳn */
    private function sane():Boolean {
        var items:Array = bag();
        var n:int = 0;
        for each (var g:Object in items) { var r:Object = matchOf(g); if (r && r.real) n++; }
        if (n > ANOMALY_COUNT || (items.length >= 10 && n > items.length * ANOMALY_RATIO)) {
            emergency("số món khớp quy tắc bất thường (" + n + "/" + items.length + " món) — kiểm tra lại quy tắc");
            return false;
        }
        return true;
    }

    private function destroyPass(now:int):void {
        if (!_bh || _bhStopped || _rules.length == 0) return;
        logDry("destroy"); logDry("sell");
        if (now - _lastActEnd < ACTION_GAP) return;
        var p:Array = plan("destroy", true);
        if (p.length == 0 || !sane()) return;
        if (_destroyed >= MAX_DESTROY) { emergency("đã hủy " + _destroyed + " món trong phiên (giới hạn " + MAX_DESTROY + ")"); return; }
        startAction("destroy", p[0].g, p[0].r, 0);
    }

    // SAFETY 8: quy tắc chạy thử chỉ ghi log
    private function logDry(act:String):void {
        for each (var x:Object in plan(act, false)) {
            var k:String = act + fp(x.g);
            if (_dryLogged[k]) continue;
            _dryLogged[k] = true;
            audit("DRY-" + (act == "sell" ? "SELL" : "DESTROY"), x.g, x.r);
        }
    }

    private function startAction(act:String, g:Object, r:Object, npcId:int):void {
        // SAFETY 5: đọc lại đúng ô đó ngay trước khi gửi, phải giống hệt
        var cur:Object = null;
        try { cur = gi().mainCharData.goodsInfo.getGoodsByPosition(g.position); } catch (e:Error) { }
        if (cur == null || fp(cur) != fp(g) || matchOf(cur) != r) { _skipFp[fp(g)] = true; return; }
        if (!_sender) { emergency("không có hàm gửi của game (Item_MsgSenderProxy)"); return; }
        _act = { act: act, g: g, r: r, fp: fp(g), pos: int(g.position), id: int(g.id), at: getTimer(), totals: totals(),
                 copper: copper(), npc: npcId, count: Math.max(1, int(g.count)) };
        if (act == "destroy") _sender.send_11181([g.position, 0]);
        else _sender.send_10883([npcId, g.id, g.count, g.position]);
    }

    // SAFETY 6: một món một lần, chờ xác nhận; món khác biến mất => dừng
    private function checkAction(now:int):void {
        // So theo MÓN, không theo ô: túi có dồn ô / xếp lại / gộp chồng vẫn đúng.
        var a:Object = _act;
        var now2:Object = totals();
        for (var k:String in a.totals) {
            var allow:Number = int(k) == a.id ? a.count : 0;          // mã của món mục tiêu được giảm đúng số lượng của nó
            var after:Number = now2[k] == undefined ? 0 : now2[k];
            if (after < a.totals[k] - allow) {
                _act = null;
                emergency("một món khác (mã " + k + ") bị giảm từ " + a.totals[k] + " còn " + after + " khi đang "
                    + (a.act == "sell" ? "bán" : "hủy") + " " + gname(a.g));
                return;
            }
        }
        var stillThere:Boolean = bag().indexOf(a.g) >= 0;
        var tgtAfter:Number = now2[a.id] == undefined ? 0 : now2[a.id];
        var gone:Boolean = !stillThere && tgtAfter <= a.totals[a.id] - a.count;
        if (gone) {
            _act = null; _lastActEnd = now; _fails = 0;
            if (a.act == "destroy") { _destroyed++; audit("DESTROY", a.g, a.r); }
            else {
                _sold++;
                var got:Number = copper() - a.copper;
                if (got > 0) _mSold += got;
                audit("SELL", a.g, a.r, got > 0 ? " money=" + got : "");
            }
            return;
        }
        if (now - a.at > ACTION_WAIT) {
            _act = null; _lastActEnd = now;
            _skipFp[a.fp] = true;
            if (a.act == "sell" && _trip) _trip.retry.push(a.fp);
            note("bh", "warn không " + (a.act == "sell" ? "bán" : "hủy") + " được " + gname(a.g) + " (ô " + a.pos + "), bỏ qua");
            if (++_fails >= MAX_FAILS) emergency(MAX_FAILS + " lần liên tiếp không xác nhận được");
        }
    }

    /** tổng số lượng theo mã vật phẩm trong túi */
    private function totals():Object {
        var t:Object = {};
        for each (var o:Object in bag()) if (o) t[int(o.id)] = (t[int(o.id)] == undefined ? 0 : t[int(o.id)]) + Math.max(1, int(o.count));
        return t;
    }

    // SAFETY 10: dừng khẩn cấp
    private function emergency(why:String):void {
        _bhStopped = why;
        _act = null;
        note("bh", "stop " + why);
    }

    // SAFETY 9: nhật ký — panel ghi ra data\banhuy_<acc>.log
    private function audit(tag:String, g:Object, r:Object, extra:String = ""):void {
        var res:Object = g.res || {};
        note("bh", "audit " + tag + " name=" + enc(gname(g)) + " id=" + g.id + " o=" + g.position + " sl=" + g.count
            + " q=" + g.quality + " sao=" + g.strengthen_grade + " khoa=" + g.bind + " kind=" + res.kind
            + " cap=" + res.grade + " capdung=" + res.limit_grade + " phai=" + res.popsinger + " vitri=" + res.position
            + " rule=" + (r ? r.idx : 0) + extra);
    }

    private function bagDump():String {
        var out:Array = [];
        for each (var g:Object in bag()) {
            if (!g) continue;
            var res:Object = g.res || {};
            var rec:String = "o=" + g.position + " id=" + g.id + " ten=" + gname(g) + " sl=" + g.count + " q=" + g.quality
                + " sao=" + g.strengthen_grade + " khoa=" + g.bind + " kind=" + res.kind + " cap=" + res.grade
                + " capdung=" + res.limit_grade + " phai=" + res.popsinger + " vitri=" + res.position
                + " ben=" + g.curr_durability + "/" + res.durability + " is_sale=" + res.is_sale + " discard=" + res.discard
                + " hoihuy=" + (g.isShowDestroyPanel ? 1 : 0) + "/" + res.f_discard_is_ui;
            var pr:String = protectedReason(g);
            var r:Object = matchOf(g);
            rec += pr ? " [bảo vệ: " + pr + "]" : r ? " [khớp quy tắc " + r.idx + ": " + (r.act == "sell" ? "bán" : "hủy") + (r.real ? "" : ", chạy thử") + "]" : "";
            out.push(enc(rec));
        }
        return "ok " + out.join(",");
    }

    // ------------------------------------------------------------------ sửa đồ / về thành

    private function needRepair():Boolean {
        try {
            for each (var g:Object in gi().mainCharData.goodsInfo.goodsBodyArr)
                if (g && g.res && int(g.res.kind) == 2 && int(g.res.durability) > 0 && int(g.curr_durability) == 0) return true;
        } catch (e:Error) { }
        return false;
    }

    private function minDurPct():int {
        var m:int = 100;
        try {
            for each (var g:Object in gi().mainCharData.goodsInfo.goodsBodyArr)
                if (g && g.res && int(g.res.kind) == 2 && int(g.res.durability) > 0 && int(g.curr_durability) >= 0)
                    m = Math.min(m, int(g.curr_durability * 100 / g.res.durability));
        } catch (e:Error) { }
        return m;
    }

    /** train gọi định kỳ khi đang train (ngoài phó bản): lý do cần về thành, hoặc null */
    public function townReason(now:int):String {
        if (_trip || _act || now - _tripFailAt < TRIP_COOLDOWN || isFuben(curMap())) return null;
        if (_repair && needRepair()) return "trang bị hỏng (độ bền 0), về thành sửa";
        if (_bh && !_bhStopped) {
            var free:int = freeSlots();
            var p:Array = plan("sell", true);
            var eq:Boolean = false, it:Boolean = false;
            for each (var x:Object in p) { if (x.r.kind == "equip") eq = true; else it = true; }
            if ((eq && free <= _freeE) || (it && free <= _freeI))
                return "túi còn " + free + " ô trống, về thành bán " + p.length + " món";
        }
        return null;
    }

    public function townBegin(now:int):void {
        _trip = { phase: "go", npc: NPC_VUKHI, since: now, lastWalk: 0, step: 0, stepAt: 0, retry: [], repaired: false, sold: false };
    }

    public function townAbort():void { _trip = null; }

    /** true = xong (hoặc bỏ), train quay lại điểm */
    public function townStep(now:int):Boolean {
        var t:Object = _trip;
        if (!t) return true;
        if (now - t.since > TRIP_TIMEOUT) { note("util", "warn về thành quá lâu, bỏ chuyến này (thử lại sau 10 phút)"); return endTrip(now, true); }
        if (_act) return false;
        if (t.phase == "go") {
            if (curMap() != TOWN_MAP && teleTo(TOWN_MAP, now)) return false;
            var npc:Object = npcChar(t.npc);
            if (npc && tileDist(me(), npc.tile_x, npc.tile_y) <= 3.5) {
                gi().lockOnChar = npc;
                t.phase = t.npc == NPC_VUKHI ? "repair" : "sell";
                t.stepAt = now; t.step = 0;
                return false;
            }
            if (now - t.lastWalk >= 6000 && me().getStatus() != "walk") {
                _c.MainCharSeachPathManager.mainCharWalk(t.npc, false, null, true, true, false);
                t.lastWalk = now;
            }
            return false;
        }
        var n:Object = npcChar(t.npc);
        if (!n) { t.phase = "go"; return false; }
        if (t.phase == "repair") {
            if (!_repair || !needRepair()) {
                if (t.step == 2) {
                    var cost:Number = t.cp - copper();
                    if (cost > 0) _mRep += cost;
                    note("util", "info sửa đồ xong (" + (_repairType == 2 ? "sửa đặc biệt" : "sửa thường") + ")" + (cost > 0 ? ", tốn " + cost + " đồng" : ""));
                }
                t.phase = "sell"; return false;
            }
            if (t.step == 0) { t.cp = copper(); _sender.send_50221(n.id); t.step = 1; t.stepAt = now; return false; }
            if (t.step == 1 && now - t.stepAt >= 600) { _sender.send_50223([_repairType, n.id]); t.step = 2; t.stepAt = now; return false; }
            if (t.step == 2) {
                if (now - t.stepAt > 4000) { note("util", "error sửa đồ không được (thiếu đồng?)"); _repair = false; t.phase = "sell"; }
            }
            return false;
        }
        if (t.phase == "sell") {
            if (!_bh || _bhStopped) return endTrip(now, false);
            if (now - _lastActEnd < ACTION_GAP) return false;
            var p:Array = plan("sell", true);
            if (p.length > 0) {
                if (!sane()) return endTrip(now, false);
                if (_sold >= MAX_SELL) { emergency("đã bán " + _sold + " món trong phiên (giới hạn " + MAX_SELL + ")"); return endTrip(now, false); }
                startAction("sell", p[0].g, p[0].r, n.id);
                return false;
            }
            // món Vũ Khí không nhận: thử ở Tạp Hóa một lần
            if (t.npc == NPC_VUKHI && t.retry.length > 0) {
                for each (var f:String in t.retry) delete _skipFp[f];
                t.retry = []; t.npc = NPC_TAPHOA; t.phase = "go"; t.lastWalk = 0;
                return false;
            }
            return endTrip(now, false);
        }
        return endTrip(now, false);
    }

    private function endTrip(now:int, failed:Boolean):Boolean {
        _trip = null;
        if (failed) _tripFailAt = now;
        return true;
    }

    private function npcChar(id:int):Object {
        try { return gi().scene.getCharByID(id, 6); } catch (e:Error) { }
        return null;
    }

    /**
     * Truyền tống miễn phí khi có VIP (Võ Lâm Lệnh). Không có VIP thì KHÔNG BAO GIỜ gửi.
     * true = đang chờ truyền tống (đừng đi bộ).
     */
    public function teleTo(map:int, now:int):Boolean {
        var t:Object = _teleState;
        if (t && t.map == map) {
            if (curMap() == map) { _teleState = null; return false; }
            if (t.step == 1 && now - t.at < 3000) return true;                 // chờ 10132
            if (t.step == 2 && now - t.at < TELE_WAIT) return true;            // chờ chuyển map
            _teleState = null; _teleFailAt = now;
            var why:String = _notice.at >= t.start ? " — server: " + _notice.msg : "";
            note("util", "warn truyền tống không thành" + (t.step == 1 ? " (server không trả thông tin map)" : "") + why + ", đi bộ");
            return false;
        }
        if (!_tele || !isVip() || !_uiSender || now - _teleFailAt < TELE_RETRY || isFuben(curMap()) || curMap() == map) return false;
        var id:int = map;
        try {
            var n:String = String(_c.MapTransManager.getSceneNameBySceneId(map));
            var sid:int = int(_c.MapTransManager.getSmallestSceneId(n));
            if (sid > 0) id = sid;
        } catch (e:Error) { }
        if (!isVip()) return false;                          // kiểm lần cuối
        // Bước 1 giống bấm tên map trên bản đồ thế giới: hỏi server (10131), server trả 10132 -> bước 2 gửi 10133.
        if (!_hooked) hookNet();
        _uiSender.send_10131({ id: id });
        _teleState = { map: map, at: now, start: now, step: 1, id: id };
        note("util", "info truyền tống (VIP) tới map " + id);
        return true;
    }

    private function hookNet():void {
        try {
            _c.NetWorkManager.registerMsg(22222, onNotice, "VlcmUtil_22222");
            _c.NetWorkManager.registerMsg(10132, onPortalInfo, "VlcmUtil_10132");
            _hooked = true;
        } catch (e:Error) { _hooked = true; note("util", "warn không theo dõi được thông báo server: " + e.message); }
    }

    private function onNotice(n:Object):void {
        var b:ByteArray = n.body as ByteArray;
        var p:uint = b.position;
        try { b.position = 4; b.readUTF(); _notice = { at: getTimer(), msg: b.readUTF(), used: false }; }
        catch (e:Error) { } finally { b.position = 4; }
    }

    private function onPortalInfo(n:Object):void {
        var t:Object = _teleState;
        if (!t || t.step != 1) return;
        var b:ByteArray = n.body as ByteArray;
        var id:int = t.id;
        try { b.position = 4; id = b.readInt(); } catch (e:Error) { } finally { b.position = 4; }
        if (!isVip()) { _teleState = null; return; }         // không bao giờ truyền tống tốn phí
        _uiSender.send_10133({ id: id });                   // như bấm nút truyền tống trên bảng
        t.step = 2; t.at = getTimer();
        _portalCloseUntil = t.at + 4000;                    // game cũng hiện bảng hỏi chi phí (MapPortalPanel) từ 10132: đóng như nút Hủy
    }

    /**
     * Thông báo server mới (chưa dùng) đến sau thời điểm t, dùng một lần. Trả:
     *   "others" = "vật phẩm của người khác, thử nhặt lại sau 15 giây"
     *   "far"    = "cách vật phẩm quá xa, không thể nhặt"
     *   "other"  = thông báo khác (ghi log một lần, không ảnh hưởng nhặt)
     *   null     = không có
     */
    public function pickNotice(t:int, now:int):String {
        if (_notice.used || _notice.at < t || now - _notice.at > 1500) return null;
        _notice.used = true;
        var m:String = String(_notice.msg).toLowerCase();
        if (m.indexOf("của người khác") >= 0) return "others";
        if (m.indexOf("quá xa") >= 0) return "far";
        if (m.indexOf("không thể nhặt") >= 0) return "cant";
        if (m.indexOf("đầy") >= 0 && (m.indexOf("túi") >= 0 || m.indexOf("hành trang") >= 0)) return "full";
        if (!_noticeSeen[_notice.msg]) {
            _noticeSeen[_notice.msg] = true;
            note("util", "info khi nhặt, server báo: \"" + _notice.msg + "\"");
        }
        return "other";
    }

    // ------------------------------------------------------------------ đóng cửa sổ

    /** bảng hỏi chi phí truyền tống (MapPortalPanel trong POPWindowManager): đóng như nút Hủy của bảng */
    private function closePortalPanel():void {
        try {
            var PW:Object = _c.POPWindowManager;
            var w:Object = PW ? PW.popWindow : null;
            if (!w || !w.parent || w.numChildren == 0) return;
            if (getQualifiedClassName(w.getChildAt(0)).indexOf("MapPortalPanel") < 0) return;
            PW.closeWindow();
            _portalCloseUntil = 0;
            note("util", "info đã đóng bảng truyền tống");
        } catch (e:Error) { }
    }

    /** như phím Esc của game (KeyboardManager): FPanel -> sự kiện "close"; BasePanel -> onClose(). Bỏ qua bảng thẻ quà. */
    private function closeStartupPanels():void {
        var ui:Object = null;
        try { ui = gi().uiInstance; } catch (e:Error) { }
        if (!ui) return;
        var box:Object = null, gift:Object = null;
        try { box = ui.uiContainer; gift = ui._cardGiftPanel; } catch (e2:Error) { }
        if (!box) return;
        var FP:Class = _c.FPanel as Class, BP:Class = _c.BasePanel as Class, FE:Class = _c.FCloseEvent as Class;
        for (var i:int = int(box.numChildren) - 1; i >= 0; i--) {
            var ch:Object = null;
            try { ch = box.getChildAt(i); } catch (e3:Error) { continue; }
            if (!ch || !ch.visible || ch == gift) continue;
            var name:String = getQualifiedClassName(ch);
            try {
                if (FP && FE && ch is FP) ch.dispatchEvent(new FE("close", null));
                else if (BP && ch is BP) ch.onClose();
                else continue;
            } catch (e4:Error) { continue; }
            var shortName:String = name.substr(name.lastIndexOf(":") + 1);
            if (!_uiClosed[shortName]) { _uiClosed[shortName] = true; note("util", "info đã đóng cửa sổ " + shortName); }
        }
    }

    // ------------------------------------------------------------------ cài đặt trong game

    private function applyGameSettings():void {
        _gsMap = curMap();
        for (var k:String in _gs) {
            var want:Boolean = _gs[k];
            var had:Boolean = _gsApplied[k] == true;
            if (want == had || (!want && _gsApplied[k] == undefined)) continue;    // chưa từng bật thì không đụng
            try { pipe("SETTING_DATA", [int(k), want]); _gsApplied[k] = want; } catch (e:Error) { }
        }
        if (_hpfx != _hpfxApplied) {
            try { _c.GameState.hideOtherPlayerHpEffect = _hpfx; _hpfxApplied = _hpfx; } catch (e2:Error) { }
        }
    }

    // ------------------------------------------------------------------ hiển thị / hiệu năng

    private static function newBucket():Object {
        return { ms: 0, frames: 0, fpsSet: 0, lagSum: 0, lagN: 0, lagMax: 0, skills: 0, kills: 0, pickSum: 0, pickN: 0, walkT: 0, walkMs: 0 };
    }
    private function bucket():Object { return _perf[_render == "hide2" ? 2 : _render == "hide1" ? 1 : 0]; }
    public function perfSkill():void { bucket().skills++; }
    public function perfKill():void { bucket().kills++; }
    public function perfPick(ms:int):void { if (ms > 0 && ms < 60000) { var b:Object = bucket(); b.pickSum += ms; b.pickN++; } }
    /** trễ nhịp của vòng train (định kỳ expected ms) */
    public function perfTick(now:int, expected:int):void { }

    private function onFrame(e:Event):void { _frames++; }

    private function perfUtil(now:int):void {
        var st:Object = null;
        try { st = gi().stage; } catch (e:Error) { }
        if (st && !_stageHooked) { st.addEventListener(Event.ENTER_FRAME, onFrame); _stageHooked = true; }
        var b:Object = bucket();
        if (_lastUtilTick > 0) {
            var lag:int = Math.max(0, now - _lastUtilTick - TICK_MS);
            b.lagSum += lag; b.lagN++; if (lag > b.lagMax) b.lagMax = lag;
        }
        _lastUtilTick = now;
        var dt:int = now - _lastPerf;
        if (dt >= 1000) {
            b.ms += dt; b.frames += _frames; _frames = 0; _lastPerf = now;
            try { b.fpsSet = st ? int(st.frameRate) : 0; } catch (e2:Error) { }
        }
        try {                                               // tốc độ đi (ô/giây) khi đang đi
            var m:Object = me();
            if (m && m.getStatus() == "walk" && !isNaN(_walkX)) {
                var dx:Number = m.tile_x - _walkX, dy:Number = m.tile_y - _walkY;
                var dd:Number = Math.sqrt(dx * dx + dy * dy);
                if (dd < 5) { b.walkT += dd; b.walkMs += TICK_MS; }
            }
            if (m) { _walkX = m.tile_x; _walkY = m.tile_y; }
        } catch (e3:Error) { }
    }

    /** ẩn: tắt vẽ hình (giữ nguyên nhịp game); hide2: thêm hạ khung hình còn 15 */
    private function render(mode:String):String {
        var st:Object = null;
        try { st = gi().stage; } catch (e:Error) { }
        if (!st) return "err chưa có stage";
        if (mode != "hide1" && mode != "hide2") mode = "show";
        if (mode != "show" && _saved == null) {
            _saved = { quality: st.quality, fps: st.frameRate, vis: [] };
            for (var i:int = 0; i < st.numChildren; i++) { var c:Object = st.getChildAt(i); _saved.vis.push({ c: c, v: c.visible }); c.visible = false; }
            st.quality = "low";
        }
        if (_saved) st.frameRate = mode == "hide2" ? Math.min(15, _saved.fps) : _saved.fps;
        if (mode == "show" && _saved) {
            for each (var s:Object in _saved.vis) try { s.c.visible = s.v; } catch (e2:Error) { }
            st.quality = _saved.quality; st.frameRate = _saved.fps;
            _saved = null;
        }
        _render = mode;
        return "ok render=" + mode;
    }

    // ------------------------------------------------------------------ tiện ích

    private function note(n:String, d:String):void { _emit(n == "train" ? "train" : "util", n == "train" ? d : n + " " + d); }

    private function pipe(name:String, body:Object = null):void { _c.PipeManager.sendMsg(name, body); }

    private function cooling(item:Object):Boolean {
        try { return _c.CDFaceManager != null && _c.CDFaceManager.isCooling(item.getID()); } catch (e:Error) { }
        return false;
    }

    private function isFuben(id:int):Boolean {
        try { return _c.MapTransManager && int(_c.MapTransManager.getMapRes(id).isFuben) == 1; } catch (e:Error) { }
        return false;
    }

    private function gi():Object { return _c.GameInstance; }

    private function me():Object {
        try { return gi().mainChar; } catch (e:Error) { }
        return null;
    }

    private function curMap():int {
        try { var s:Object = gi().scene; if (s && s.mapConfig) return s.mapConfig.mapID; } catch (e:Error) { }
        return -1;
    }

    private static function isDead(c:Object):Boolean {
        if (c.getStatus() == "death") return true;
        try { return c.data.attributeInfo.isDeath(); } catch (e:Error) { }
        return false;
    }

    private static function tileDist(c:Object, x:int, y:int):Number {
        var dx:Number = c.tile_x - x, dy:Number = c.tile_y - y;
        return Math.sqrt(dx * dx + dy * dy);
    }

    private static function enc(s:String):String { return encodeURIComponent(s); }
    private static function trim(s:String):String { return s ? s.replace(/^\s+|\s+$/g, "") : ""; }

    private static function parseArgs(s:String):Object {
        var o:Object = {};
        if (!s) return o;
        for each (var part:String in s.split(" ")) {
            var i:int = part.indexOf("=");
            if (i > 0) o[part.substr(0, i).toLowerCase()] = part.substr(i + 1);
        }
        return o;
    }

    private static function intList(s:String):Array {
        var out:Array = [];
        for each (var p:String in String(s).split(",")) if (trim(p) != "") out.push(int(p));
        return out;
    }
}
}
