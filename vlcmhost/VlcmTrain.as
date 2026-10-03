package {
import flash.events.TimerEvent;
import flash.events.MouseEvent;
import flash.geom.Point;
import flash.utils.ByteArray;
import flash.utils.getQualifiedClassName;
import flash.utils.Timer;
import flash.utils.setTimeout;
import flash.utils.getTimer;

/**
 * VlcmTrain: train tại chỗ, tự viết hoàn toàn (không dùng treo máy AFK của game).
 *
 * Mọi thao tác đi qua luồng có sẵn của client, giống người chơi thao tác:
 *   đi (kể cả khác map)  MainCharSeachPathManager.mainCharWalk("map,x,y,0")   (tự tìm đường như bấm link nhiệm vụ)
 *   đánh thường          PipeManager.sendMsg("HIT_CHAR", [quái, true, true])   (như click quái; game tự đuổi, tự kiểm cooldown)
 *   dùng skill           PipeManager.sendMsg("USE_SKILL", skill)              (bước cuối của useShortKey, sau khi game kiểm CD)
 *   bấm phím tắt         PipeManager.sendMsg("KEY_n")                         (như bấm phím số n)
 *   nhặt đồ              PipeManager.sendMsg("PICK_UP", idTúi)                (như click túi đồ trên đất)
 *   hồi sinh về thành    gói 20075 [byte 1, double getTimer()]                (y hệt nút "Về thành hồi sinh")
 * Không gửi gói dùng skill tự chế: skill bị client từ chối (đang hồi, thiếu MP, ngoài tầm) thì bỏ qua.
 *
 * Dữ liệu đọc từ game (xác minh trong mã TGame):
 *   ô phím tắt n    ExchangePositionManager.checkItemShortIndex(n)  -> Skill | Goods
 *   đang hồi chiêu  CDFaceManager.isCooling(item.getID())           (đúng hàm useShortKey kiểm)
 *   tốn MP          skill.res.depletion_parameter == 2  ->  skill.skillWaste()
 *   tầm skill       skill.res.distance (ô; game đổi ra pixel = distance * 25)
 *   túi đồ rơi      gói 11160 / 11162 (thêm) và 11164 (xóa); tên = GoodsResManager.getGoodsRes(id).name
 *   phó bản         MapTransManager.getMapRes(map).isFuben == 1
 *
 * Lệnh (từ host qua vlcm_command):
 *   where | status | train_stop | skills | bag_names
 *   train_start [k=v ...]       không có map/x/y thì lấy vị trí đang đứng
 *       map= x= y=  r=8           điểm train (ô) và bán kính quanh điểm (ô)
 *       skillmode=slots|list|keys slots: skill chủ động ở ô 1-5 (bỏ 50000); list: skills=; keys: bấm keys= (tối đa 10 lần/giây)
 *       skills=51013,51014        danh sách id skill cho chế độ list
 *       keys=1,2,3                phím cho chế độ keys
 *       hpkey=9  hp=40            phím bình máu, bơm khi máu dưới hp%
 *       types=1,2                 loại quái được đánh (MonsterType: 1 thường, 2 tinh anh, 3 boss). Luôn giữ bộ lọc
 *                                 để không đánh xe tiêu, cờ bang, tượng, cây ước nguyện...
 *       restdeaths=0  restmin=5   chết đủ N lần thì sau khi hồi sinh ở thành đứng nghỉ; 0 = tắt
 *       pick=off|list|all         nhặt đồ trong bán kính train (nhặt trước, xong mới đánh)
 *       picklist=<mã hóa URL>     danh sách tên, cách nhau dấu phẩy, [ ] tùy ý; đứng trước ưu tiên hơn.
 *                                 [Đồng] = tiền rơi; [Mảnh] = tên bắt đầu bằng "Mảnh "; re:<regex> = biểu thức;
 *                                 còn lại khớp khi tên chứa từ khóa (không phân biệt hoa thường).
 *
 *   (hpkey=/hp= vẫn nhận để tương thích; thuốc nay do VlcmUtil lo — lệnh util_set)
 *
 * Lệnh tiện ích (VlcmUtil): util_set k=v..., bag_dump, bh_stop, bh_reset, stats_reset.
 * Trạng thái town: về thành sửa/bán rồi quay lại điểm train.
 * Hồi chiêu chung: sau khi skill ra chiêu, khóa cả nhóm (getPublicCoolingtimeClass) đúng getPublicCoolingtime()
 * vì CDFaceManager.isCooling(id) chỉ thấy hồi chiêu riêng.
 *
 * Giá trị chữ trả về (tên nhân vật, tên map, danh sách) được mã hóa URL (encodeURIComponent).
 */
public class VlcmTrain {

    public static const IDLE:String = "idle";
    public static const GOING:String = "going";
    public static const FIGHTING:String = "fighting";
    public static const DEAD:String = "dead";
    public static const RESTING:String = "resting";
    public static const TOWN:String = "town";              // về thành sửa / bán (VlcmUtil)
    public static const PB:String = "pb";                  // đang chạy phó bản (VlcmTrain phần PHÓ BẢN)

    private static const TICK_MS:int = 100;
    private static const HIT_EVERY:int = 1000;         // HIT_CHAR tối đa 1 lần/giây
    private static const SKILL_GAP:int = 200;          // chế độ slots/list: khoảng cách giữa 2 lần dùng skill
    private static const KEY_GAP:int = 100;            // chế độ keys: tối đa 10 lần/giây
    private static const POTION_GAP:int = 1000;
    private static const WALK_RETRY:int = 5000;
    private static const TARGET_GIVEUP:int = 20000;
    private static const BLACKLIST_MS:int = 60000;
    private static const TARGET_HARDCAP:int = 45000;   // chốt cứng: 45s kể từ lúc chọn mà máu chưa giảm lần nào (vd. sau vật cản)
    private var _hpTgtId:int = -1, _hpDropAt:int = 0;
    private static const REVIVE_FIRST:int = 2000;
    private static const REVIVE_RETRY:int = 8000;
    private static const REVIVE_MAX:int = 6;
    private static const AFTER_REVIVE:int = 2000;
    private static const STATUS_EVERY:int = 5000;
    private static const NO_TARGET_WARN:int = 30000;
    // nhặt đồ
    private static const PICK_ATTEMPTS:int = 3;        // đi tới túi + nhặt: tối đa 3 lần, không được -> tạm bỏ 10s
    private static const PICK_IDLE:int = 3000;         // đứng yên 3s mà túi còn -> thử lại lần sau
    private static const PICK_STUCK:int = 8000;        // 8s không lại gần hơn -> kẹt, tạm bỏ 10s
    private static const PICK_OTHERS:int = 15000;      // server báo túi của người khác -> thử lại sau 15s
    private static const PICK_RETRY:int = 10000;
    private static const PICK_ANIM:int = 1500;         // chờ diễn xong chiêu tối đa 1.5s rồi mới đi nhặt
    private static const SUPPORT_GAP:int = 1000;
    private static const PUB_MARGIN:int = 150;
    private static const BASIC_SKILL:int = 50000;
    private static const DROP_OPS:Array = [11160, 11162, 11164];
    private static const DROP_CTX:String = "VlcmTrain_drops";

    private var _c:Object;
    private var _emit:Function;
    private var _timer:Timer;

    private var _state:String = IDLE;
    private var _map:int, _x:int, _y:int;
    private var _r:int = 8;
    private var _skillMode:String = "slots";
    private var _skillIds:Array = [];
    private var _keys:Array = [1, 2, 3];
    private var _hpKey:int = -1;
    private var _hpPct:int = 40;
    private var _types:Array = [1, 2];
    private var _restDeaths:int = 0;
    private var _restMin:Number = 5;
    private var _pickMode:String = "off";
    private var _pickRules:Array = [];

    private var _deathsSinceRest:int = 0;
    private var _restUntil:int = 0;
    // Mục tiêu đang đánh. Game TÁI SỬ DỤNG đối tượng nhân vật (ScenePool): con quái chết/rời cảnh thì đối tượng đó
    // có thể được dùng lại cho nhân vật khác (hay gặp nhất: ngựa chiến của chính mình, loại 3). Vì vậy luôn nhớ
    // loại + id lúc chọn và coi mục tiêu là "đã mất" nếu đối tượng đổi loại/id.
    private var _tgt:Object = null, _tgtId:int = -1, _tgtBoss:Boolean = false, _tgtGoneLogAt:int = -60000, _tgtGone:int = 0;
    private function get _target():Object { return (_tgt != null && !mobRefOk(_tgt, _tgtId)) ? null : _tgt; }
    private function set _target(v:Object):void {
        _tgt = v;
        _tgtId = -1; _tgtBoss = false;
        if (v != null) { try { _tgtId = int(v.id); _tgtBoss = isBossMob(v); } catch (e:Error) { } }
    }
    /** đối tượng vẫn đúng là con quái (loại 2) có id đã nhớ, còn trong cảnh */
    private static function mobRefOk(c:Object, id:int):Boolean {
        try { return c != null && c.usable && int(c.type) == 2 && int(c.id) == id; } catch (e:Error) { }
        return false;
    }
    /** mục tiêu khóa hợp lệ để bot gán cho game: quái (2) hoặc NPC (6) hoặc chính mình; không bao giờ ngựa chiến/đồng hành */
    private function safeLock(c:Object):Object {
        try { if (c != null && c.usable && (int(c.type) == 2 || int(c.type) == 6 || c == me())) return c; } catch (e:Error) { }
        return null;
    }
    /** đầu mỗi nhịp: mục tiêu đã bị game tái sử dụng (đổi loại/id) thì bỏ, gỡ khóa và dừng lệnh đuổi theo cũ của game */
    private function guardRefs(now:int):void {
        var lk:Object = null;
        try { lk = gi().lockOnChar; } catch (e:Error) { }
        var lockBad:Boolean = false;
        try { lockBad = lk != null && (!lk.usable || int(lk.type) == 3 || int(lk.type) == 4 || int(lk.type) == 8); } catch (e1:Error) { lockBad = true; }
        if (_tgt != null && !mobRefOk(_tgt, _tgtId)) {
            var wasLock:Boolean = lk == _tgt;
            var kind:int = -1;
            try { kind = int(_tgt.type); } catch (e2:Error) { }
            if (_tgtBoss || _state == FIGHTING || _state == PB) {                 // con cũ biến mất khỏi cảnh = đã chết: tính kill + chờ đồ rơi như thường
                _kills++; _util.perfKill();
                if (!(_state == PB && _pbRun && _pbRun.key == "lt")) _dw = { id: _tgtId, until: now + (_tgtBoss ? 2000 : 1000), boss: _tgtBoss };
            }
            _tgt = null; _tgtId = -1; _tgtBoss = false; _ap = null;
            if (wasLock) lockBad = true;
            stopChase(wasLock || kind == 3);
            _tgtGone++;
            if (now - _tgtGoneLogAt > 30000) { _tgtGoneLogAt = now; _emit("train", "info mục tiêu cũ đã rời cảnh (game dùng lại đối tượng" + (kind == 3 ? " cho ngựa chiến" : "") + ") — đã bỏ khóa, chọn con khác (lần " + _tgtGone + ")"); }
        }
        if (lockBad) {
            try { gi().lockOnChar = null; } catch (e3:Error) { }
            stopChase(lk != null && int(lk.type) == 3);
        }
    }
    /** hủy lệnh đi đuổi mục tiêu của game (MoveCallBack còn giữ đối tượng cũ -> sẽ đánh nhầm ngựa chiến khi tới nơi) */
    private function stopChase(force:Boolean):void {
        if (!force) return;
        var m:Object = me();
        try { if (m.getStatus() != "walk") return; } catch (e:Error) { return; }
        try { _c.MainCharSeachPathManager.clear(); } catch (e1:Error) { }
        try { m.stopMove(); } catch (e2:Error) { }
    }
    private var _targetSince:int, _lastHit:int, _lastSkill:int, _lastPotion:int, _lastWalk:int;
    private var _rot:int = 0;
    private var _black:Object = {};
    private var _deadAt:int, _reviveTries:int, _aliveAt:int;
    private var _kills:int, _deaths:int, _errors:int, _picked:int;
    private var _lastStatus:int, _lastEligible:int, _lastWarn:int;

    private var _drops:Object = {};            // idTúi -> {name, money}
    private var _dropsHooked:Boolean = false;
    private var _dropsMap:int = -1;            // map hiện tại của _drops
    private var _dropsSweepAt:int = 0;         // lúc dọn _drops tiếp theo
    private var _pickBag:Object;
    private var _pickName:String = "";
    private var _pickSince:int, _pickLastSend:int, _pickLastWalk:int, _pickTries:int, _pickBest:Number, _pickProgress:int;
    private var _pickIdleAt:int;               // lúc bắt đầu đứng yên (không đi, không diễn chiêu) trong lần thử hiện tại
    private var _pickBlack:Object = {};        // idTúi -> hạn bỏ qua (int.MAX = cả phiên)
    private var _pickInfo:Object;              // {name, money, amt} của túi đang nhặt
    private var _pickCopper:Number = 0;        // số đồng lúc bắt đầu nhặt túi đó

    private var _util:VlcmUtil;
    private var _lastTownCheck:int;
    // hồi chiêu chung theo nhóm (Skill.getPublicCoolingtimeClass): CDFaceManager.isCooling không thấy phần này
    private var _pubUntil:Object = {};         // nhóm hồi chung -> khóa tạm (chờ xác nhận ra chiêu, tối đa 1s)
    private var _skLock:Object = {};           // id skill -> hết hồi chung riêng của skill đó (như playMainCharPublicCoolingtime)
    private var _castPend:Array = [];          // các lần ra chiêu chờ xác nhận
    // nhóm kỹ năng (skillmode=sets): 4 nhóm theo thứ tự ưu tiên + hỗ trợ
    private var _groups:Array = [[], [], [], []];
    private var _heal:Array = [];              // [{id, pct}]
    private var _buffs:Array = [];             // [id]
    private var _supMp:int = 30;
    private var _lastSupport:int;
    private var _myMobs:Object = {};           // id quái bot đã đánh (đồ rơi từ chúng = đồ của mình)
    private var _pickRefused:int;
    private var _lastDropMob:Object = {};     // id quái -> lúc nhận gói túi đồ nó rơi

    public function VlcmTrain(classes:Object, emit:Function) {
        _c = classes;
        _emit = emit;
        _timer = new Timer(TICK_MS);
        _timer.addEventListener(TimerEvent.TIMER, onTick);
        _util = new VlcmUtil(classes, emit);
    }

    // ------------------------------------------------------------------ lệnh

    public function command(cmd:String, args:String):String {
        var u:String = _util.command(cmd, args);
        if (u != null) return u;
        switch (cmd) {
            case "where":       return where();
            case "status":      return status();
            case "train_stop":  stop("dừng theo lệnh"); return "ok " + status();
            case "train_start": return start(parseArgs(args));
            case "skills":      return listSkills();
            case "bag_names":   return listBagNames();
            case "pb_start":    return pbStart(parseArgs(args));
            case "pb_stop":     pbStop("dừng theo lệnh"); return "ok";
            case "pb_list":     return pbList(parseArgs(args));
        }
        return "err lệnh không hỗ trợ: " + cmd;
    }

    /** cài đặt đánh / hỗ trợ / nhặt (dùng chung cho train_start và pb_start) */
    private function applyFightCfg(kv:Object):void {
        if (kv.skillmode != undefined) _skillMode = String(kv.skillmode);
        if (kv.skills != undefined) _skillIds = intList(kv.skills);
        if (kv.keys != undefined) _keys = intList(kv.keys);
        if (kv.hpkey != undefined) _hpKey = int(kv.hpkey);
        if (kv.hp != undefined) _hpPct = int(kv.hp);
        if (kv.types != undefined) _types = intList(kv.types);
        if (kv.restdeaths != undefined) _restDeaths = Math.max(0, int(kv.restdeaths));
        if (kv.restmin != undefined) _restMin = Math.max(0.1, Number(kv.restmin));
        if (kv.pick != undefined) _pickMode = String(kv.pick);
        if (kv.picklist != undefined) _pickRules = parseRules(decodeURIComponent(String(kv.picklist)));
        for (var gk:int = 0; gk < 4; gk++) if (kv["g" + (gk + 1)] != undefined) _groups[gk] = intList(kv["g" + (gk + 1)]);
        if (kv.heal != undefined) {
            _heal = [];
            for each (var hp:String in String(kv.heal).split(",")) {
                var hc:int = hp.indexOf(":");
                if (hc > 0) _heal.push({ id: int(hp.substr(0, hc)), pct: int(hp.substr(hc + 1)) });
            }
        }
        if (kv.buff != undefined) _buffs = intList(kv.buff);
        if (kv.supmp != undefined) _supMp = int(kv.supmp);
        if (_hpKey >= 0 && _keys.indexOf(_hpKey) >= 0) {
            _keys.splice(_keys.indexOf(_hpKey), 1);
            _emit("train", "warn phím " + _hpKey + " là phím bình máu, đã bỏ khỏi danh sách phím skill");
        }
        if (kv.hpkey != undefined || kv.hp != undefined)                  // lệnh cũ: chuyển cho phần thuốc
            _util.command("util_set", "pot=keys hpkey=" + _hpKey + " hp=" + _hpPct);
    }
    /** bộ kỹ năng riêng của phó bản (<k>_g1..g4, _heal, _buff, _supmp, _set) — null nếu panel không gửi */
    private static function pbSkillCfg(kv:Object, key:String):Object {
        if (kv[key + "_g1"] == undefined && kv[key + "_heal"] == undefined) return null;
        var o:Object = { setNo: int(kv[key + "_set"]) };
        for (var g:int = 1; g <= 4; g++) o["g" + g] = kv[key + "_g" + g] != undefined ? kv[key + "_g" + g] : "";
        o.heal = kv[key + "_heal"] != undefined ? kv[key + "_heal"] : "";
        o.buff = kv[key + "_buff"] != undefined ? kv[key + "_buff"] : "";
        if (kv[key + "_supmp"] != undefined) o.supmp = kv[key + "_supmp"];
        return o;
    }
    private function fightCfgText():String {
        return "skillmode=" + _skillMode + (_skillMode == "sets" ? " nhóm=" + _groups.map(function(g:*, i:int, a:Array):String { return g.join(","); }).join("|")
               + " hồi máu=" + _heal.length + " buff=" + _buffs.length + " MP tối thiểu=" + _supMp + "%" : " phím=" + _keys.join(",")) + " nhặt=" + _pickMode;
    }

    private function start(kv:Object):String {
        if (me() == null || curMap() < 0) return "err nhân vật chưa vào map";
        if (kv.map != undefined || kv.x != undefined || kv.y != undefined) {
            if (kv.map == undefined || kv.x == undefined || kv.y == undefined) return "err cần đủ map, x, y (hoặc bỏ cả ba để lấy vị trí hiện tại)";
            _map = int(kv.map); _x = int(kv.x); _y = int(kv.y);
        } else {
            _map = curMap(); _x = me().tile_x; _y = me().tile_y;
        }
        if (kv.r != undefined) _r = Math.max(1, int(kv.r));
        applyFightCfg(kv);

        hookDrops();
        _target = null; _black = {}; _rot = 0; _pickBag = null; _castPend = []; _skLock = {}; _pubUntil = {};
        _lastWalk = 0; _lastHit = 0; _lastSkill = 0; _lastPotion = 0; _errors = 0;
        _lastEligible = getTimer(); _lastWarn = 0;
        if (_state != RESTING && _state != DEAD) _state = GOING;
        _timer.start();
        var d:String = "map=" + _map + " x=" + _x + " y=" + _y + " r=" + _r + " skillmode=" + _skillMode
            + (_skillMode == "list" ? " skills=" + _skillIds.join(",") : "") + (_skillMode == "keys" ? " keys=" + _keys.join(",") : "")
            + (_skillMode == "sets" ? " groups=" + _groups.map(function(g:*, i:int, a:Array):String { return g.join(","); }).join("|")
               + " heal=" + _heal.length + " buff=" + _buffs.length + " supmp=" + _supMp : "")
            + " hpkey=" + _hpKey + " hp=" + _hpPct + " types=" + _types.join(",")
            + " restdeaths=" + _restDeaths + " restmin=" + _restMin + " pick=" + _pickMode + " pickrules=" + _pickRules.length;
        _emit("train", "start " + d);
        diagnose();
        return "ok " + d;
    }

    private function stop(reason:String):void {
        _timer.stop();
        _util.townAbort();
        _state = IDLE;
        _target = null;
        _pickBag = null;
        _restUntil = 0;
        _emit("train", "stop " + reason);
    }

    private function where():String {
        if (me() == null || curMap() < 0) return "err nhân vật chưa vào map";
        var s:String = "map=" + curMap() + " x=" + me().tile_x + " y=" + me().tile_y;
        var mn:String = mapName(curMap());
        if (mn) s += " name=" + enc(mn);
        return "ok " + s;
    }

    private function status():String {
        var s:String = "state=" + _state;
        try {
            if (me() != null && curMap() >= 0) {
                var a:Object = gi().mainCharData.attributeInfo;
                s += " map=" + curMap() + " x=" + me().tile_x + " y=" + me().tile_y
                   + " hp=" + int(a.hpNow) + "/" + int(a.hpMax) + " mp=" + int(a.mpNow) + "/" + int(a.mpMax) + " lv=" + a.lv;
                if (me().name) s += " name=" + enc(String(me().name));
                var mn:String = mapName(curMap());
                if (mn) s += " mapname=" + enc(mn);
            }
        } catch (e:Error) { }
        s += " kills=" + _kills + " deaths=" + _deaths + " picked=" + _picked + " prefused=" + _pickRefused;
        if (_target) s += " target=" + _target.id;
        if (_pickBag) s += " picking=" + _pickBag.id;
        if (_state != IDLE) s += " point=" + _map + "," + _x + "," + _y + " r=" + _r;
        if (_state == RESTING) s += " rest=" + Math.max(0, int((_restUntil - getTimer()) / 1000));
        s += " restcount=" + _deathsSinceRest;
        s += pbStatus();
        s += lzStatus();
        s += _util.statusPart();
        return s;
    }

    // "ok id:tên:tầm:useway,..." — mọi skill đã học (panel dùng để chọn cho chế độ list)
    private function listSkills():String {
        if (me() == null) return "err nhân vật chưa vào map";
        var out:Array = [];
        var obj:Object = gi().mainCharData.skillInfo.skillObj;
        for each (var s:Object in obj) {
            try {
                if (s == null || s.res == null || s.id == BASIC_SKILL) continue;
                var pt:int = 0;
                try { pt = s.getPublicCoolingtime(); } catch (e2:Error) { }
                out.push(s.id + ":" + enc(String(s.res.name)) + ":" + s.res.distance + ":" + s.res.useway + ":" + s.res.public_type + ":" + pt);
            } catch (e:Error) { }
        }
        return "ok " + out.join(",");
    }

    // "ok tên,tên,..." — tên các món đang có trong túi (panel dùng để thêm vào danh sách nhặt)
    private function listBagNames():String {
        if (me() == null) return "err nhân vật chưa vào map";
        var seen:Object = {}, out:Array = [];
        for each (var g:Object in gi().mainCharData.goodsInfo.goodsBagArr) {
            try {
                var n:String = String(g.res.name);
                if (n && !seen[n]) { seen[n] = true; out.push(enc(n)); }
            } catch (e:Error) { }
        }
        return "ok " + out.join(",");
    }

    // Báo lúc bắt đầu: quái quanh điểm theo từng loại, và skill sẽ dùng — để biết ngay nếu đọc sai dữ liệu.
    private function diagnose():void {
        try {
            var byType:Object = {}, n:int = 0;
            for each (var c:Object in gi().scene.getCharsByType(2)) {
                if (!c.usable || tileDist(c, _x, _y) > _r) continue;
                var t:String = "?";
                try { t = String(c.data.res.type); } catch (e:Error) { }
                byType[t] = int(byType[t]) + 1;
                n++;
            }
            var parts:Array = [];
            for (var k:String in byType) parts.push("type" + k + ":" + byType[k]);
            var sk:Array = [];
            for each (var s:Object in skillCandidates()) sk.push(skillName(s));
            var keys:String = _skillMode == "keys" ? "phím " + _keys.join(",") : "skill " + (sk.length ? sk.join(", ") : "(không có)");
            _emit("train", "info quanh điểm " + n + " quái (" + (parts.length ? parts.join(", ") : "không có") + "), đánh type "
                + _types.join(",") + "; " + keys);
        } catch (err:Error) {
            _emit("train", "warn không đọc được dữ liệu chẩn đoán: " + err.message);
        }
    }

    // ------------------------------------------------------------------ vòng lặp

    private function onTick(e:TimerEvent):void {
        var now:int = getTimer();
        _util.perfTick(now, TICK_MS);
        try { step(now); }
        catch (err:Error) {
            if (++_errors % 50 == 1) _emit("train", "error " + err.message);
        }
        if (now - _lastStatus >= STATUS_EVERY) {
            _lastStatus = now;
            _emit("train", "status " + status());
        }
    }

    private function step(now:int):void {
        if (me() == null || curMap() < 0) return;
        guardRefs(now);
        confirmCasts(now);

        // Đổi map (nhất là rời phó bản) thì server không gửi 11164 cho túi ở map cũ: dọn để khỏi rò rỉ / tên rác.
        // Dọn trễ 3s (gói 11160 của map mới có thể tới trước khi scene đổi mapID), và định kỳ 60s.
        if (curMap() != _dropsMap) {
            _dropsMap = curMap();
            _pickBlack = {};
            _pickBag = null;
            _dropsSweepAt = now + 3000;
        } else if (now >= _dropsSweepAt) {
            sweepDrops();
            _dropsSweepAt = now + 60000;
        }

        trackMove(now);
        walkWatch(now);
        moveDiag(now);
        if (_state == PB) { pbStep(now); return; }
        if (isDead(me())) {
            if (_state != DEAD) {
                setState(DEAD, "chết, chờ hồi sinh về thành");
                _deaths++;
                _deathsSinceRest++;
                _deadAt = now;
                _reviveTries = 0;
                _target = null;
                _pickBag = null;
                _util.townAbort();
            }
            if (_reviveTries < REVIVE_MAX && now - _deadAt >= REVIVE_FIRST + _reviveTries * REVIVE_RETRY) {
                revive();
                _reviveTries++;
                if (_reviveTries == REVIVE_MAX) _emit("train", "error hồi sinh " + REVIVE_MAX + " lần không được, cần xử lý tay");
            }
            return;
        }
        if (_state == DEAD) {
            if (_restDeaths > 0 && _deathsSinceRest >= _restDeaths) {
                _restUntil = now + int(_restMin * 60000);
                setState(RESTING, "bị giết " + _deathsSinceRest + " lần, nghỉ " + _restMin + " phút");
                return;
            }
            setState(GOING, "đã hồi sinh, quay lại điểm train");
            _aliveAt = now;
            _lastWalk = 0;
            return;
        }
        if (_state == RESTING) {
            if (now < _restUntil) return;
            _deathsSinceRest = 0;
            _restUntil = 0;
            setState(GOING, "hết giờ nghỉ, quay lại điểm train");
            _aliveAt = now;
            _lastWalk = 0;
            return;
        }
        if (_state == GOING && _aliveAt > 0 && now - _aliveAt < AFTER_REVIVE) return;

        if (_state == TOWN) {
            if (_util.townStep(now)) { setState(GOING, "xong việc ở thành, quay lại điểm train"); _lastWalk = 0; }
            return;
        }
        if (now - _lastTownCheck >= 2000) {
            _lastTownCheck = now;
            var why:String = _util.townReason(now);
            if (why) {
                _target = null; _pickBag = null;
                _util.townBegin(now);
                setState(TOWN, why);
                return;
            }
        }

        var here:int = curMap();
        var far:Boolean = here != _map || tileDist(me(), _x, _y) > _r;
        if (far) {
            // đang đuổi quái / đi nhặt đồ trong vùng thì cho lệch thêm một chút
            if (here == _map && tileDist(me(), _x, _y) <= _r + 6) {
                if (_pickBag != null && pickStep(now)) return;
                if (_target != null && valid(_target)) { fight(now); return; }
            }
            if (_state != GOING) setState(GOING, "đi về điểm train");
            _target = null;
            _pickBag = null;
            if (here != _map && _util.teleTo(_map, now)) return;          // VIP: truyền tống miễn phí rồi đi tiếp
            if (now - _lastWalk >= WALK_RETRY && me().getStatus() != "walk") {
                walkTo(_map, _x, _y);
                _lastWalk = now;
            }
            return;
        }

        if (_state != FIGHTING) { setState(FIGHTING, "tới điểm train"); _aliveAt = 0; }
        if (support(now)) return;
        if (pickStep(now)) return;                 // nhặt đồ trước, nhặt xong mới đánh
        fight(now);
    }

    // ------------------------------------------------------------------ đánh

    private function fight(now:int):void {
        if (_target != null && !valid(_target)) {
            if (isDead(_target)) { _kills++; _util.perfKill(); noteKill(_target, now); }
            _target = null;
        }
        if (_target == null) {
            if (dropWaiting(now)) return;                                   // vừa giết: chờ đồ rơi, nhặt xong mới chọn con mới
            _target = pick(now);
            if (_target == null) { warnNoTarget(now); return; }
            _targetSince = now; _targetHp = mobHp(_target);
            _lastEligible = now;
            _myMobs[_target.id] = now;
            gi().lockOnChar = _target;
        }
        _lastEligible = now;
        // bỏ mục tiêu chỉ khi đánh KHÔNG hiệu quả: 20s ĐANG ĐÁNH mà máu không giảm (boss thì không bao giờ bỏ).
        // Chỉ đếm lúc đang đánh thật vào con đó: còn đi lại gần / đang đi / bị khống chế / vừa buff-heal thì đồng hồ đứng yên.
        var hp:Number = mobHp(_target);
        if (_hpTgtId != _target.id) { _hpTgtId = _target.id; _hpDropAt = now; }
        if (hp >= 0 && (_targetHp < 0 || hp < _targetHp)) { _targetHp = hp; _targetSince = now; _hpDropAt = now; }
        if (_oor || me().getStatus() == "walk" || charFixed() || now - _lastSupport < 1500) _targetSince = now;
        var giveUp:String = null;
        if (now - _targetSince > TARGET_GIVEUP) giveUp = int(TARGET_GIVEUP / 1000) + " giây đánh mà máu không giảm";
        else if (now - _hpDropAt > TARGET_HARDCAP) giveUp = int(TARGET_HARDCAP / 1000) + " giây kể từ lúc chọn mà máu không giảm";
        if (giveUp && !isBossMob(_target)) {
            var ban:int = _state == PB ? 10000 : BLACKLIST_MS;
            _black[_target.id] = now + ban;
            _emit("train", "info bỏ quái #" + _target.id + " cách " + tileDist(me(), _target.tile_x, _target.tile_y).toFixed(1) + " ô: " + giveUp + " — bỏ qua " + int(ban / 1000) + " giây");
            _target = null;
            return;
        }
        if (useSkill(now)) { _ap = null; return; }
        if (_oor) { approach(now, _oorStop); return; }                    // có skill sẵn sàng nhưng quái ngoài tầm: đi lại gần trước
        if (now - _lastHit >= (_fast ? 400 : HIT_EVERY) && me().getStatus() != "attack") {
            pipe("HIT_CHAR", [_target, true, true]);
            _lastHit = now;
        }
    }

    private var _targetHp:Number = -1;
    private static function mobHp(c:Object):Number {
        try { var a:Object = c.data.attributeInfo; if (a && a.hpNow != undefined) return Number(a.hpNow); } catch (e:Error) { }
        return -1;
    }
    // ---- vừa giết: chờ gói rơi đồ của đúng con đó (quái 1s, boss 2s; boss chờ thêm 1s sau túi cuối)
    private var _dw:Object = null;
    private function noteKill(t:Object, now:int):void {
        if (_state == PB && _pbRun && _pbRun.key == "lt") return;            // Liên Trảm: giữ nhịp chuỗi
        var boss:Boolean = isBossMob(t);
        _dw = { id: t.id, until: now + (boss ? 2000 : 1000), boss: boss };
    }
    private function dropWaiting(now:int):Boolean {
        if (_dw == null) return false;
        var last:* = _lastDropMob[_dw.id];
        if (last != undefined) {
            if (!_dw.boss) { _dw = null; return false; }
            _dw.until = int(last) + 1000;                                     // boss rơi nhiều đợt: chờ 1s sau túi cuối
        }
        if (now >= _dw.until) { _dw = null; return false; }
        return true;
    }

    // true nếu vừa dùng skill / bấm phím
    private function useSkill(now:int):Boolean {
        _oor = false; _oorStop = 0;
        if (_skillMode == "keys") {
            if (_keys.length == 0 || now - _lastSkill < KEY_GAP) return false;
            for (var i:int = 0; i < _keys.length; i++) {
                var k:int = _keys[(_rot + i) % _keys.length];
                var item:Object = slotItem(k);
                if (item != null && cooling(item)) continue;           // đọc được ô thì bỏ qua phím đang hồi
                if (item != null && item.res && _target != null && !skillInRange(item, _target)) { _oor = true; _oorStop = Math.max(_oorStop, skillStop(item)); continue; }
                gi().lockOnChar = _target;
                pipe("KEY_" + k);
                _rot = (_rot + i + 1) % _keys.length;
                _lastSkill = now;
                return true;
            }
            return false;
        }
        confirmCasts(now);
        if (now - _lastSkill < (_fast ? 0 : SKILL_GAP)) return false;
        var list:Array = skillCandidates();
        var prio:Boolean = _skillMode == "sets" && hasSets();       // nhóm kỹ năng: dùng skill đầu tiên sẵn sàng theo thứ tự
        for (var j:int = 0; j < list.length; j++) {
            var s:Object = list[prio ? j : (_rot + j) % list.length];
            if (!skillReady(s)) continue;
            if (_target != null && !skillInRange(s, _target)) { _oor = true; _oorStop = Math.max(_oorStop, skillStop(s)); continue; }
            gi().lockOnChar = _target;
            pipe("USE_SKILL", s);
            var cls:String = pubClass(s), pt:int = pubTime(s);
            if (cls && pt > 0) _pubUntil[cls] = now + 1000;   // khóa nhóm tạm thời tới khi xác nhận
            _castPend.push({ s: s, at: now, cls: pt > 0 ? cls : null, pt: pt });
            _rot = (_rot + j + 1) % list.length;
            _lastSkill = now;
            return true;
        }
        return false;
    }

    // slots: skill chủ động đã học ở ô 1-5, bỏ đánh thường 50000.   list: skill theo id đã chọn.
    private function hasSets():Boolean {
        for each (var g:Array in _groups) if (g.length) return true;
        return false;
    }

    private function skillCandidates():Array {
        var out:Array = [];
        if (_skillMode == "sets" && hasSets()) {
            for each (var grp:Array in _groups)
                for each (var gid:int in grp) {
                    var gs:Object = skillById(gid);
                    if (isActiveSkill(gs) && out.indexOf(gs) < 0) out.push(gs);
                }
            return out;
        }
        if (_skillMode == "slots" || _skillMode == "sets") {
            for (var n:int = 1; n <= 5; n++) {
                var it:Object = slotItem(n);
                if (isActiveSkill(it)) out.push(it);
            }
        } else if (_skillMode == "list") {
            for each (var id:int in _skillIds) {
                var s:Object = null;
                try { s = gi().mainCharData.skillInfo.getSkillByID(id); } catch (e:Error) { }
                if (isActiveSkill(s)) out.push(s);
            }
        }
        return out;
    }

    private function skillById(id:int):Object {
        try { return gi().mainCharData.skillInfo.getSkillByID(id); } catch (e:Error) { }
        return null;
    }

    // Hỗ trợ: hồi máu khi HP dưới mốc, buff khi hết buff; MP dưới _supMp% thì ngưng. Dùng lên bản thân.
    private function support(now:int):Boolean {
        if (_skillMode != "sets" || (_heal.length == 0 && _buffs.length == 0) || now - _lastSupport < SUPPORT_GAP) return false;
        confirmCasts(now);
        var a:Object = gi().mainCharData.attributeInfo;
        if (a.mpMax <= 0 || a.mpNow * 100 < a.mpMax * _supMp) return false;
        var mv:Boolean = moving(now);
        var pick:Object = null, isHeal:Boolean = false;
        for each (var h:Object in _heal) {
            if (a.hpMax > 0 && a.hpNow * 100 < a.hpMax * h.pct) {
                // đang đi: heal cách nhau >= 3s (mỗi lần heal game dừng di chuyển); máu dưới nửa mốc thì heal ngay
                if (mv && now - _lastHealAt < 3000 && a.hpNow * 200 >= a.hpMax * h.pct) continue;
                var hs:Object = skillById(h.id);
                if (hs && selfReady(hs) && !supBanned(hs, now)) { pick = hs; isHeal = true; break; }
            }
        }
        if (!pick && !mv) for each (var bid:int in _buffs) {               // đang đi / vừa gửi lệnh đi: không buff
            var bs:Object = skillById(bid);
            if (!bs || supBanned(bs, now) || !selfReady(bs)) continue;
            if (_supOkAt[bs.id] != undefined && now - int(_supOkAt[bs.id]) < buffGap(bs)) continue;   // vừa buff thành công: chưa buff lại
            if (!hasBuffOf(bs)) { pick = bs; break; }
        }
        if (!pick) return false;
        if (isHeal) _lastHealAt = now;
        var keep:Object = gi().lockOnChar;
        gi().lockOnChar = me();
        pipe("USE_SKILL", pick);
        gi().lockOnChar = safeLock(_target != null ? _target : keep);
        // game dừng bước khi ra chiêu (stopMove): xóa mốc chống spam để nhịp sau gửi lại lệnh đi ngay; đi tuần không tính đoạn bị dừng
        _mv = null;
        if (_pbRun) { _pbRun.moveAt = 0; if (_pbRun.gp) _pbRun.gp.prog = now + 1000; }
        _lastSupport = now;
        _lastSkill = now;
        var cls:String = pubClass(pick), pt:int = pubTime(pick);
        if (cls && pt > 0) _pubUntil[cls] = now + 1000;
        _castPend.push({ s: pick, at: now, cls: pt > 0 ? cls : null, pt: pt, sup: true, heal: isHeal });
        return true;
    }
    private var _lastHealAt:int = -60000;
    private var _supOkAt:Object = {};          // skill hỗ trợ -> lúc tung thành công gần nhất
    private var _supFail:Object = {};          // skill hỗ trợ -> số lần liền không tung được
    private var _supBan:Object = {};           // skill hỗ trợ -> tạm bỏ tới
    private var _supLogAt:Object = {};
    private function supBanned(s:Object, now:int):Boolean { return _supBan[s.id] != undefined && int(_supBan[s.id]) > now; }
    /** khoảng tối thiểu giữa 2 lần buff cùng skill: thời gian tác dụng của buff (đọc được), không thì 30s */
    private static function buffGap(s:Object):int {
        var d:Number = 0;
        try { d = Number(s.buffres.duration); } catch (e:Error) { }
        if (d >= 5000 && d <= 3600000) return int(d * 0.9);
        if (d >= 5 && d <= 3600) return int(d * 900);
        return 30000;
    }
    private function supResult(c:Object, ok:Boolean, now:int):void {
        var s:Object = c.s, nm:String = s.res ? String(s.res.name) : "#" + s.id;
        if (ok) {
            _supOkAt[s.id] = now; _supFail[s.id] = 0;
            if (_supLogAt[s.id] == undefined || now - int(_supLogAt[s.id]) > 30000) { _supLogAt[s.id] = now; _emit("train", "info hỗ trợ: dùng " + (c.heal ? "hồi máu" : "buff") + " \"" + nm + "\""); }
            return;
        }
        var n:int = int(_supFail[s.id]) + 1;
        _supFail[s.id] = n;
        if (n >= 2) {
            _supFail[s.id] = 0; _supBan[s.id] = now + 120000;
            _emit("train", "warn hỗ trợ: \"" + nm + "\" không tự tung lên bản thân được (game cần bật PK hoặc giữ Shift, hoặc thiếu điều kiện) — tạm bỏ 2 phút");
        }
    }

    // xác nhận lần ra chiêu: skill vào hồi chiêu = đã ra chiêu -> khóa nhóm đúng thời gian hồi chung
    private function confirmCasts(now:int):void {
        var keep:Array = [];
        for each (var c:Object in _castPend) {
            if (cooling(c.s)) {
                if (c.cls) { lockSameClass(c.s, c.cls, c.at); if (_pubUntil[c.cls] == c.at + 1000) delete _pubUntil[c.cls]; }
                _util.perfSkill();
                if (c.sup) supResult(c, true, now);
            }
            else if (now - c.at > 1200) { if (c.cls && _pubUntil[c.cls] == c.at + 1000) delete _pubUntil[c.cls]; if (c.sup) supResult(c, false, now); }
            else keep.push(c);
        }
        _castPend = keep;
    }

    /**
     * Game (Fight_proxy.playMainCharPublicCoolingtime): skill vừa ra chiêu tự hồi max(hồi riêng, hồi chung) — isCooling thấy;
     * mỗi skill KHÁC cùng nhóm hồi chung bị khóa theo getPublicCoolingtime() của CHÍNH skill đó, tính từ lúc ra chiêu.
     */
    private function lockSameClass(cast:Object, cls:String, at:int):void {
        var all:Object = null;
        try { all = gi().mainCharData.skillInfo.skillObj; } catch (e:Error) { }
        if (!all) return;
        for each (var o:Object in all) {
            if (o == null || o == cast) continue;
            var pt:int = pubTime(o);
            if (pt <= 0 || pubClass(o) != cls) continue;
            var until:int = at + pt + PUB_MARGIN;          // cộng chút trễ mạng: gửi sớm sẽ bị từ chối
            if (_skLock[o.id] == undefined || _skLock[o.id] < until) _skLock[o.id] = until;
        }
    }

    private function pubLocked(s:Object, now:int):Boolean {
        var cls:String = pubClass(s);
        if (cls && _pubUntil[cls] != undefined && _pubUntil[cls] > now) return true;
        return _skLock[s.id] != undefined && _skLock[s.id] > now;
    }

    private function selfReady(s:Object):Boolean {
        if (cooling(s)) return false;
        if (pubLocked(s, getTimer())) return false;
        try { if (s.res.depletion_parameter == 2 && s.skillWaste() > gi().mainCharData.attributeInfo.mpNow) return false; } catch (e:Error) { }
        return true;
    }

    /** buff do skill này tạo ra còn trên người không (id buff = res.effect_ids) */
    private function hasBuffOf(s:Object):Boolean {
        var bi:Object = null;
        try { bi = gi().mainCharData.buffInfo; } catch (e:Error) { }
        if (!bi) try { bi = me().data.buffInfo; } catch (e2:Error) { }
        if (!bi) return false;
        try {
            for each (var id:String in String(s.res.effect_ids).split(",")) if (int(id) > 0 && bi.hasBuff(int(id))) return true;
            var nm:String = s.buffres ? String(s.buffres.name) : "";
            if (nm) for each (var b:Object in bi.buffArr) if (b && b.res && String(b.res.name) == nm) return true;
        } catch (e3:Error) { }
        return false;
    }

    private static function isActiveSkill(s:Object):Boolean {
        try { return s != null && s.res != null && s.id != BASIC_SKILL && s.res.useway == 1; }
        catch (e:Error) { }
        return false;
    }

    // Không hồi chiêu, đủ MP, mục tiêu trong tầm (theo dữ liệu tầm của game).
    private function skillReady(s:Object):Boolean {
        if (cooling(s)) return false;
        if (pubLocked(s, getTimer())) return false;                    // đang hồi chung (theo thời gian riêng của skill)
        var a:Object = gi().mainCharData.attributeInfo;
        try { if (s.res.depletion_parameter == 2 && s.skillWaste() > a.mpNow) return false; } catch (e:Error) { }
        return true;
    }

    // ---- tầm đánh: đo như game (pixel, tầm skill × 25); skill đánh quanh mình (target 1/2/11) thì quái phải cách <= 3 ô
    private var _oor:Boolean = false;          // lần dùng skill vừa rồi: có skill sẵn sàng nhưng quái ngoài tầm
    private var _oorStop:int = 0;              // khoảng cách dừng (pixel) khi đi lại gần
    private var _ap:Object = null;             // đang đi lại gần mục tiêu {t, best, prog, at, x, y}
    private static function selfCast(s:Object):Boolean {
        try {
            var t:int = int(s.res.target);
            if (t == 1 || t == 2 || t == 11) return true;
            if ((t == 3 || t == 4) && s.buffres && int(s.buffres.user_scope) == 2 && int(s.buffres.aoe_type) == 1) return true;
        } catch (e:Error) { }
        return false;
    }
    private function pxDist(t:Object):Number {
        try {
            var m:Object = me();
            if (m.piexl_x != undefined && t.piexl_x != undefined) { var dx:Number = Number(t.piexl_x) - Number(m.piexl_x), dy:Number = Number(t.piexl_y) - Number(m.piexl_y); return Math.sqrt(dx * dx + dy * dy); }
        } catch (e:Error) { }
        return tileDist(me(), t.tile_x, t.tile_y) * 25;
    }
    private function skillInRange(s:Object, t:Object):Boolean {
        if (t == null || s == null || !s.res) return true;
        if (_forceId == t.id && getTimer() - _forceAt < 3000 && !selfCast(s)) return true; // đi không nhích (game coi là đã tới): cho ra chiêu nhắm mục tiêu 3s
        if (tileDist(me(), t.tile_x, t.tile_y) <= 1) return true;         // sát bên: luôn coi là trong tầm
        if (selfCast(s)) return tileDist(me(), t.tile_x, t.tile_y) <= 3;
        var r:Number = Number(s.res.distance) * 25;
        if (r <= 0) r = 50;
        return pxDist(t) <= r + 15;
    }
    /** khoảng cách dừng khi đi lại gần: lọt hẳn vào trong tầm (60% tầm), tối thiểu 1 ô (~40px) để điểm đến không rơi ngay chân */
    private static function skillStop(s:Object):int {
        if (selfCast(s)) return 40;
        var r:int = int(Number(s.res.distance) * 25 * 0.6);
        return r < 40 ? 40 : r;
    }
    /** quái ngoài tầm: dùng lệnh đi của game kèm khoảng cách dừng (như attackByPointOK); 3s không lại gần được thì bỏ con đó 10s */
    private function approach(now:int, stop:int):void {
        var t:Object = _target;
        if (t == null) return;
        var d:Number = tileDist(me(), t.tile_x, t.tile_y);
        if (_ap == null || _ap.t != t) _ap = { t: t, best: d, prog: now, at: 0, x: -999, y: -999, tx0: t.tile_x, ty0: t.tile_y };
        if (d < _ap.best - 0.5 || busyAnim(me())) { if (d < _ap.best) _ap.best = d; _ap.prog = now; }
        if (now - _ap.prog > 1500 && isBossMob(t)) {                      // boss (hất văng / nhảy): không bỏ, ra chiêu nhắm boss, game tự đi tới
            _forceId = t.id; _forceAt = now; _ap.prog = now; _ap.at = 0;
            return;
        }
        // quái đã ở gần (<= 6 ô) hoặc đang di chuyển (aggro, đuổi theo): không bỏ — ra chiêu nhắm thẳng, game tự đi tới
        if (now - _ap.prog > 1500 && (d <= AP_NEAR || Math.abs(t.tile_x - _ap.tx0) + Math.abs(t.tile_y - _ap.ty0) >= 2)) {
            _forceId = t.id; _forceAt = now; _ap.prog = now; _ap.at = 0;
            return;
        }
        if (now - _ap.prog > 1500) {
            var n:int = int(_blackN[t.id]) + 1;
            _blackN[t.id] = n;
            var ban:int = _state == PB || n < 2 ? 10000 : 600000;        // phó bản: tối đa 10s; train: lần 2 không tới được bỏ 10 phút
            _black[t.id] = now + ban;
            _emit("train", "info bỏ quái #" + t.id + " cách " + d.toFixed(1) + " ô: 1,5 giây không lại gần được (lần " + n + ") — bỏ qua "
                  + (ban >= 60000 ? int(ban / 60000) + " phút" : int(ban / 1000) + " giây"));
            _target = null; gi().lockOnChar = null; _ap = null;
            return;
        }
        if (charFixed()) { _ap.prog = now; return; }                    // bị định thân / ngủ: không spam lệnh đi
        // điểm đến cách chân chưa tới ~1 ô (quái đã gần sát tầm dừng): không gửi lệnh đi vô ích, ra chiêu luôn
        if (pxDist(t) - stop < 30) { _forceId = t.id; _forceAt = now; return; }
        // lần đi trước 0,5s mà nhân vật không nhích (game coi như đã tới): ra chiêu luôn, game tự xử lý như khi bấm skill vào quái
        if (_ap.at > 0 && now - _ap.at >= 500 && me().getStatus() != "walk" && me().tile_x == _ap.mx && me().tile_y == _ap.my) { _forceId = t.id; _forceAt = now; _ap.at = 0; return; }
        var moved:Boolean = Math.abs(t.tile_x - _ap.x) + Math.abs(t.tile_y - _ap.y) > 2;
        if (now - _ap.at >= 500 && (me().getStatus() != "walk" || moved)) {
            if (moveTo(curMap(), t.tile_x, t.tile_y, stop, "tới quái")) { _ap.at = now; _ap.x = t.tile_x; _ap.y = t.tile_y; _ap.mx = me().tile_x; _ap.my = me().tile_y; }
        }
    }
    private static const AP_NEAR:Number = 6;     // quái trong 6 ô: không bao giờ bỏ vì "không lại gần được"
    private var _blackN:Object = {};
    private var _forceId:int = -1, _forceAt:int = 0;  // quái được "coi như trong tầm" (đi lại gần không nhích)           // số lần quái bị bỏ vì không lại gần được

    private static function pubClass(s:Object):String {
        try { return String(s.getPublicCoolingtimeClass()); } catch (e:Error) { }
        return null;
    }
    private static function pubTime(s:Object):int {
        try { return int(s.getPublicCoolingtime()); } catch (e:Error) { }
        return 0;
    }

    private function cooling(item:Object):Boolean {
        try { return _c.CDFaceManager != null && _c.CDFaceManager.isCooling(item.getID()); } catch (e:Error) { }
        return false;
    }

    private function slotItem(n:int):Object {
        try { return _c.ExchangePositionManager ? _c.ExchangePositionManager.checkItemShortIndex(n) : null; } catch (e:Error) { }
        return null;
    }

    private function skillName(s:Object):String {
        var n:String = String(s.id);
        try { n = s.res.name + "#" + s.id; } catch (e:Error) { }
        return n;
    }

    private function pick(now:int):Object {
        var best:Object = null, bestD:Number = Number.MAX_VALUE;
        for each (var c:Object in gi().scene.getCharsByType(2)) {
            if (!valid(c) || !typeOk(c)) continue;
            if (_black[c.id] != undefined && _black[c.id] > now) continue;
            var d:Number = tileDist(me(), c.tile_x, c.tile_y);
            if (d < bestD) { bestD = d; best = c; }
        }
        return best;
    }

    // Có quái trong vùng mà lâu không chọn được con nào: thường do sai loại quái hoặc đọc sai dữ liệu.
    private function warnNoTarget(now:int):void {
        if (now - _lastEligible < NO_TARGET_WARN || now - _lastWarn < 60000) return;
        var n:int = 0;
        for each (var c:Object in gi().scene.getCharsByType(2)) if (c.usable && !isDead(c) && tileDist(c, _x, _y) <= _r) n++;
        if (n == 0) return;
        _lastWarn = now;
        _emit("train", "warn " + n + " quái trong vùng nhưng " + int((now - _lastEligible) / 1000) + " giây không chọn được con nào (kiểm tra loại quái)");
        diagnose();
    }

    private function valid(c:Object):Boolean {
        return c != null && c.usable && !isDead(c) && tileDist(c, _x, _y) <= _r;
    }

    private function typeOk(c:Object):Boolean {
        if (_state == PB) return !(_pbNoBoss && isBossMob(c));      // phó bản: đánh mọi loại quái (cả boss), trừ lúc Liên Trảm đang tích chuỗi
        try { return _types.indexOf(int(c.data.res.type)) >= 0; } catch (e:Error) { }
        return false;
    }

    // ------------------------------------------------------------------ nhặt đồ

    private function hookDrops():void {
        if (_dropsHooked) return;
        try {
            for each (var op:int in DROP_OPS) _c.NetWorkManager.registerMsg(op, onDropMsg, DROP_CTX + op);
            _dropsHooked = true;
        } catch (e:Error) { _emit("train", "warn không theo dõi được đồ rơi: " + e.message); }
    }

    private function onDropMsg(n:Object):void {
        var b:ByteArray = n.body as ByteArray;
        b.position = 4;
        try {
            var op:int = int(n.name);
            if (op == 11164) { delete _drops[b.readInt()]; return; }
            var mob:int = op == 11162 ? b.readInt() : -1;       // id quái làm rơi
            var id:int = b.readInt();
            b.readShort(); b.readShort();                       // x, y
            var goodsId:int = b.readInt();
            var money:int = b.readInt();
            var name:String = "";
            if (goodsId == -1) name = money + " đồng";
            else try { name = String(_c.GoodsResManager.getGoodsRes(goodsId).name); } catch (e:Error) { }
            _drops[id] = { name: name, money: goodsId == -1, amt: money, mine: mob > 0 && _myMobs[mob] != undefined, at: getTimer(), mob: mob, gid: goodsId };
            if (mob > 0) _lastDropMob[mob] = getTimer();
        } catch (err:Error) {
        } finally {
            b.position = 4;
        }
    }

    // true nếu đang bận nhặt (không đánh tick này)
    private function pickStep(now:int):Boolean {
        if (_pickMode == "off") return false;
        if (_pickBag != null && !bagAlive(_pickBag)) {
            _picked++;
            if (_pickInfo && _pickInfo.at) _util.perfPick(now - _pickInfo.at);
            if (_pickInfo && _pickInfo.money) _util.expectMoney(int(_pickInfo.amt), _pickCopper, _pickName);   // tính khi đồng tăng thật
            else _emit("train", "pick ok " + _pickName);
            _pickBag = null;
            if (_state != PB && isFuben(curMap())) {            // phó bản (train thường): lấy chỗ đang đứng làm điểm train mới
                _x = me().tile_x; _y = me().tile_y;
                _emit("train", "info phó bản: điểm train mới " + _x + ":" + _y);
            }
        }
        if (_pickBag == null) {
            _pickBag = chooseBag(now);
            if (_pickBag == null) return false;
            _pickSince = now; _pickTries = 0; _pickLastSend = 0; _pickLastWalk = 0;
            _pickInfo = _drops[_pickBag.id];
            _pickCopper = _util.copper();
            _pickName = _pickInfo && _pickInfo.name ? _pickInfo.name : "#" + _pickBag.id;
            _pickBest = tileDist(me(), _pickBag.tile_x, _pickBag.tile_y); _pickProgress = now;
            if (_target != null && isDead(_target)) { _kills++; _util.perfKill(); }
            _target = null;
            gi().lockOnChar = null;                             // bỏ khóa mục tiêu để game không tự đánh tiếp
        }
        // đang diễn chiêu thì lệnh đi/nhặt bị bỏ qua: chờ diễn xong (tối đa PICK_ANIM)
        var st:String = me().getStatus();
        if (st == "attack" && now - _pickSince < PICK_ANIM) { _pickProgress = now; return true; }
        var d:Number = tileDist(me(), _pickBag.tile_x, _pickBag.tile_y);
        if (d < _pickBest - 0.5) { _pickBest = d; _pickProgress = now; }
        // như game (MainChar_MainCharProxy / PickUp_PickUpProxy): đi tới ĐÚNG ô túi, tới nơi (hoặc không đi tiếp được) mới gửi nhặt MỘT lần
        if (_pickTries == 0) { pickAttempt(now); return true; }
        // thông báo sau lệnh nhặt
        var nt:String = _pickLastSend > 0 ? _util.pickNotice(_pickLastSend - 50, now) : null;
        if (nt == "others") {                                   // "vật phẩm của người khác, thử nhặt lại sau 15 giây"
            _pickRefused++;
            _pickBlack[_pickBag.id] = now + PICK_OTHERS;
            _pickBag = null;
            return true;
        }
        var moving:Boolean = st == "walk" || st == "attack";
        if (moving) _pickIdleAt = 0;
        else if (_pickIdleAt == 0) _pickIdleAt = now;
        // "quá xa" hoặc đứng yên đủ lâu mà túi vẫn còn: thử lại (đi + nhặt), hết lượt thì tạm bỏ
        if (nt == "far" && d <= 1.5 && spacePick(now)) { _pickLastSend = now; return true; }   // sát túi vẫn báo xa: phím Space của game
        if (nt == "far" || (_pickIdleAt > 0 && now - _pickIdleAt >= PICK_IDLE)) {
            if (_pickTries >= PICK_ATTEMPTS) { giveUpBag(now); return true; }
            pickAttempt(now);
            return true;
        }
        // không có MoveCallBack: tự gửi nhặt khi đã đứng đúng ô túi
        if (_c.MoveCallBack == null && _pickLastSend < _pickLastWalk && !moving && d < 0.5) sendPick(_pickBag.id);
        if (now - _pickProgress > PICK_STUCK) giveUpBag(now);
        return true;
    }

    private function giveUpBag(now:int):void {
        _pickBlack[_pickBag.id] = now + PICK_RETRY;
        if (isFuben(curMap()) && now - _pickProgress > PICK_STUCK)
            walkTo(curMap(), me().tile_x + (Math.random() < 0.5 ? -1 : 1), me().tile_y);   // bước lệch 1 ô
        _pickBag = null;
    }

    private function sendPick(id:int):void {
        if (_pickBag == null || _pickBag.id != id) return;
        pipe("PICK_UP", id);
        _pickLastSend = getTimer();
    }

    /** một lần thử: đi tới đúng ô của túi; tới nơi / không đi tiếp được thì nhặt (callback giống game) */
    private function pickAttempt(now:int):void {
        _pickTries++;
        _pickLastWalk = now; _pickIdleAt = 0;
        var bag:Object = _pickBag, id:int = bag.id;
        var spot:String = curMap() + "," + bag.tile_x + "," + bag.tile_y + ",0";
        var MCB:Class = _c.MoveCallBack as Class;
        if (MCB) {
            var self:VlcmTrain = this;
            var cb:Object = new MCB();
            cb.onMoveArrived = function(sc:Object = null, tile:Object = null):void {
                try { if (bag.usable && bag.type == 11) self.sendPick(id); } catch (e:Error) { }
            };
            cb.onMoveUnable = function(sc:Object = null, tile:Object = null):void { };   // không đi tới được: không gửi nhặt (tránh "quá xa")
            walkCmd(spot, cb, "nhặt", bag.tile_x, bag.tile_y, false);
        } else {
            walkCmd(spot, null, "nhặt", bag.tile_x, bag.tile_y, false);
        }
    }

    /** Xóa các túi trong _drops không còn trên scene. */
    private function sweepDrops():void {
        var alive:Object = {};
        try { for each (var c:Object in gi().scene.getCharsByType(11)) alive[c.id] = true; }
        catch (e:Error) { return; }
        for (var k:String in _drops) if (!alive[k]) delete _drops[k];
        var old:int = getTimer() - 120000;
        for (var mk:String in _myMobs) if (_myMobs[mk] < old) delete _myMobs[mk];
    }

    private function chooseBag(now:int):Object {
        var best:Object = null, bestRank:int = int.MAX_VALUE, bestD:Number = Number.MAX_VALUE;
        for each (var c:Object in gi().scene.getCharsByType(11)) {
            if (!c.usable || tileDist(c, _x, _y) > _r) continue;
            if (_pickBlack[c.id] != undefined && _pickBlack[c.id] > now) continue;
            var info:Object = _drops[c.id];
            var rank:int = _pickMode == "all" ? 0 : (info ? ruleRank(info) : -1);
            if (rank < 0 && _state == PB && info && info.money) rank = 0;   // phó bản: đồng luôn nhặt (chuột rơi đồng)
            if (rank < 0) continue;
            if (!(info && info.mine)) rank += 1000;             // đồ do mình giết ra nhặt trước
            var d:Number = tileDist(me(), c.tile_x, c.tile_y);
            if (rank < bestRank || (rank == bestRank && d < bestD)) { best = c; bestRank = rank; bestD = d; }
        }
        return best;
    }

    private function bagAlive(c:Object):Boolean {
        try { return c.usable && gi().scene.getCharByID(c.id, 11) == c; } catch (e:Error) { }
        return false;
    }

    // Chỉ số luật đầu tiên khớp (nhỏ = ưu tiên), -1 nếu không khớp.
    private function ruleRank(info:Object):int {
        var name:String = String(info.name);
        var low:String = name.toLowerCase();
        for (var i:int = 0; i < _pickRules.length; i++) {
            var r:Object = _pickRules[i];
            if (r.kind == "money" ? info.money
                : r.kind == "prefix" ? low.indexOf(r.v) == 0
                : r.kind == "regex" ? r.re.test(name)
                : low.indexOf(r.v) >= 0) return i;
        }
        return -1;
    }

    private static const GROUPS:Object = {
        "hoa hồng": "^Hoa\\sHồng\\s(Đỏ|Vàng|Lam|Lục|Trắng|Đen)$",
        "mảnh trận pháp": "^Mảnh\\s(Lưỡng Nghi|Tam Tài|Hổ Dực|Thái Ất|Phong Thỉ|Lưu Vân|Thiên Canh|Phục Hi)$"
    };

    private static function parseRules(s:String):Array {
        var out:Array = [];
        for each (var raw:String in s.split(",")) {
            var t:String = raw.replace(/^\s+|\s+$/g, "");
            if (t.charAt(0) == "[" && t.charAt(t.length - 1) == "]") t = t.substr(1, t.length - 2).replace(/^\s+|\s+$/g, "");
            if (!t) continue;
            var low:String = t.toLowerCase();
            try {
                if (low.indexOf("re:") == 0) out.push({ kind: "regex", re: new RegExp(t.substr(3), "i") });
                else if (low == "đồng") out.push({ kind: "money" });
                else if (low == "mảnh") out.push({ kind: "prefix", v: "mảnh " });
                else if (GROUPS[low] != undefined) out.push({ kind: "regex", re: new RegExp(GROUPS[low], "i") });
                else out.push({ kind: "sub", v: low });
            } catch (e:Error) { }
        }
        return out;
    }

    // ================================================================== PHÓ BẢN
    //
    // Luồng: Tương Dương -> NPC 1656 -> mở bảng phó bản (10711, đọc 10712: lượt đã vào / tổng)
    //        -> vào (10701 [npc, map tầng đầu, 1 = thưởng thường, 1 = độ khó thấp nhất "Trung bình"])
    //        -> trong phó bản: đi tuần + đánh -> hoàn thành (10726) -> nhận thưởng (10723) -> ra cổng về 20002 -> phó bản kế.
    // Hỏi xác nhận của game (10052 "qua ải ... tốn hoa hồng", "rời phó bản") được bấm "Xác nhận" trên chính hộp thoại.
    // Chết: hồi sinh tại chỗ bằng bảng hồi sinh của game (tốn hoa) tối đa N lần, quá thì về thành = lượt thất bại.

    private static const PB_NPC:int = 1656;
    private static const PB_NPC_X:int = 58, PB_NPC_Y:int = 67;
    private static const PB_TOWN:int = 20002;
    private static const ROSE_IDS:Array = [1201, 1202, 1203, 1204, 1205, 1206];
    private static const PB_R:int = 20;                   // bán kính tìm quái / túi quanh nhân vật trong phó bản
    private static const PB_QUIET:int = 1000;             // không có mục tiêu 1s -> coi là yên
    private static const PB_HOLD:int = 12;                // giữ mục tiêu tới khi nó cách > 12 ô
    private static const PB_FLOOR_STUCK:int = 90000;      // một tầng 90s không tiến -> gỡ kẹt (không thoát: chỉ game tự đẩy ra khi hết giờ)
    private static const PB_JUMP_GAP:int = 500;
    private static const PB_JUMP_OFS:Array = [[3,0],[-3,0],[0,3],[0,-3],[2,2],[2,-2],[-2,2],[-2,-2],[4,1],[-4,1],[1,4],[1,-4]];
    private static const PB_DEF:Object = {
        lt: { name: "Liên Trảm", maps: [20032],
              route: [[40,95],[70,104],[102,106],[134,92],[148,71],[143,45],[146,19],[128,24],[93,15],[61,19],[30,31],[14,18],[17,51],[12,76]] },
        tq: { name: "Thiên Quan", maps: [20038,20039,20040,20041,20042,20043,20044,20045,20046,20047,20048,20049,20050],
              route: [[87,65],[74,57],[56,42],[71,72],[43,70],[21,57],[7,58],[11,61],[22,34],[44,21],[56,14],[73,23],[90,23],[86,42],[98,54],[64,51],[49,38]] },
        pt: { name: "Phu Tử Trận", maps: [20175],
              route: [[57,27],[47,22],[36,15],[28,22],[23,31],[35,36],[45,31],[33,25]] },
        mc: { name: "Mê Cung Trận", maps: [20033,20177,20178,20179,20180,20181,20182,20183,20184,20185,20186,20187,20188,20189,20190,20191,20192],
              route: [] },
        dt: { name: "Doanh Trại", maps: [20060,20110,20061,20111,20062,20112,20063,20113,20064,20114,20065,20115,20066,20116,20067,20117,20068,20118,20069,20119],
              route: [[12,29],[21,25],[40,10],[42,47],[23,49],[52,26],[71,27],[81,16]] }
    };

    private var _pbQueue:Array = [];          // [{key, runs, rev, minr, jump, route}]
    private var _pbDone:Object = {};          // key -> số lượt đã xong hôm nay (panel giữ, gửi lại khi bắt đầu)
    private var _pbSkip:Object = {};          // key -> lý do bỏ qua (hết lượt / thiếu hoa / không vào được)
    private var _pbRun:Object = null;         // lượt đang chạy
    private var _pbPhase:String = "";         // town | open | enter | in | reward | exit | dead
    private var _pbEnterKey:String;
    private var _pbAt:int, _pbTry:int;
    private var _pbList:Object = null;        // từ 10712: mapID tầng đầu -> {enter, total}
    private var _pbListAt:int = -1;
    private var _pbHooked:Boolean = false;
    private var _pbTeam:Object;               // Team_MsgSenderProxy của game
    private var _pbAsk:Object = null;         // 10052 {x, y, msg, at}
    private var _pbGateAt:int = -1;           // 10300 cổng mở (thời điểm)
    private var _pbDoneAt:int = -1;           // 10726
    private var _pbJumpDir:int = 0, _pbJumpAt:int = 0;
    private var _noJumpLog:Object = {};       // map đã báo "game không cho nhảy"
    private static const PT_JUMP_MAX:int = 500;    // Phu Tử (boss Khôi Khôi): giữ tầm nhảy như cũ
    private var _pbWait:Object = {};
    private var _pbCur:String = null;         // phó bản đang làm (giữ cả lúc về thành giữa 2 lượt): làm hết lượt rồi mới sang phó bản khác
    private var _pbRoseTry:int = 0, _pbRoseWait:int = 0;          // key -> đã báo chờ (điều kiện liên trảm chưa đạt) trong danh sách này
    // ---- liên trảm (12001 [short số chuỗi, 0 = đứt]; thời gian giữ chuỗi = mainCharData.lianzhanInfo.time ms, càng cao càng ngắn)
    private var _lzHooked:Boolean = false;
    private var _lzCount:int = 0;             // chuỗi hiện tại (theo 12001)
    private var _lzLastAt:int = -1;           // lúc nhận 12001 gần nhất có số > 0 (mốc tính thời gian giữ chuỗi)
    private var _lzBuffKey:String = "";       // để báo buff liên trảm 1 lần khi đổi
    private var _fast:Boolean = false;        // cứu chuỗi: ra chiêu dồn dập hơn
    private var _pbNoBoss:Boolean = false;    // Liên Trảm lúc tích chuỗi: không đánh boss (giết boss là hết phó bản)

    /** danh sách phó bản từ tham số pb_start / pb_list */
    private function pbParseQueue(kv:Object):Array {
        var queue:Array = [];
        for each (var key:String in String(kv.list || "").split(",")) {
            if (!PB_DEF[key]) continue;
            var route:Array = PB_DEF[key].route;
            if (kv[key + "_route"]) {
                var rt:Array = [];
                for each (var pt:String in String(kv[key + "_route"]).split(";")) {
                    var c2:int = pt.indexOf(":");
                    if (c2 > 0) rt.push([int(pt.substr(0, c2)), int(pt.substr(c2 + 1))]);
                }
                if (rt.length) route = rt;
            }
            var r15:Array = [];
            if (kv[key + "_r15"]) for each (var r5:String in String(kv[key + "_r15"]).split(";")) {
                var c5:int = r5.indexOf(":");
                if (c5 > 0) r15.push([int(r5.substr(0, c5)), int(r5.substr(c5 + 1))]);
            }
            var sroute:Array = [];
            if (kv[key + "_sroute"]) for each (var sp:String in String(kv[key + "_sroute"]).split(";")) {
                var c4:int = sp.indexOf(":");
                if (c4 > 0) sroute.push([int(sp.substr(0, c4)), int(sp.substr(c4 + 1))]);
            }
            queue.push({ key: key, runs: int(kv[key + "_runs"]), rev: Math.max(0, int(kv[key + "_rev"])),
                            minr: int(kv[key + "_minr"]), jump: kv[key + "_jump"] == "1", route: route, sroute: sroute, r15: r15,
                            bow: kv[key + "_bow"] != "0", farm: int(kv[key + "_farm"]), farmMin: Math.max(1, int(kv[key + "_farmmin"] || 10)),
                            farmBy: kv[key + "_farmby"] == "lz" ? "lz" : "min", farmLz: Math.max(1, int(kv[key + "_farmlz"] || 300)), skipMice: kv[key + "_skip"] == "1",
                            sf: int(kv[key + "_sf"]), nomob: int(kv[key + "_nomob"]), lzc: parseLzCond(kv[key + "_lz"]),
                            lty: int(kv[key + "_y"]), cont: kv[key + "_cont"] == "1", lure: kv[key + "_lure"] == "1", ltx: int(kv[key + "_x"]),
                            kpp: Math.max(1, int(kv[key + "_kpp"] || 2)), pick: int(kv[key + "_pick"]), skipboss: kv[key + "_skipboss"] == "1",
                            sk: pbSkillCfg(kv, key),
                            range: Math.max(1, int(kv[key + "_range"] || 99)), afk: kv[key + "_afk"] != "0", bosscount: key == "tq" ? kv[key + "_bosscount"] != "0" : kv[key + "_bosscount"] == "1" });
        }
        return queue;
    }

    private static function pbParseDone(kv:Object):Object {
        var done:Object = {};
        for each (var d:String in String(kv.done || "").split(",")) {
            var c3:int = d.indexOf(":");
            if (c3 > 0) done[d.substr(0, c3)] = int(d.substr(c3 + 1));
        }
        return done;
    }

    private function pbStart(kv:Object):String {
        if (me() == null || curMap() < 0) return "err nhân vật chưa vào map";
        _pbQueue = pbParseQueue(kv);
        if (_pbQueue.length == 0) return "err chưa chọn phó bản";
        _pbDone = pbParseDone(kv);
        _pbSkip = {}; _pbWait = {}; _pbCur = null;
        applyFightCfg(kv);                                          // kỹ năng / hồi máu / buff / nhặt của bộ đang chọn (trước đây chỉ gửi kèm train_start)
        _emit("train", "pb kỹ năng: " + fightCfgText());
        pbHook();
        hookDrops();
        _target = null; _pickBag = null; _castPend = []; _skLock = {}; _pubUntil = {};
        _util.townAbort();
        _state = PB;
        _pbRun = null;
        // vào game mà đang ở giữa phó bản đã chọn: chạy tiếp phó bản đó trước
        var here:String = pbKeyOfMap(curMap());
        if (here && pbCfg(here)) { pbBeginRun(here, true); }
        else { _pbPhase = "town"; _pbAt = getTimer(); _pbTry = 0; }
        _timer.start();
        _emit("train", "pb start " + _pbQueue.map(function(q:*, i:int, a:Array):String { return q.key; }).join(","));
        return "ok pb " + _pbPhase;
    }

    /**
     * pb_list: panel tick / bỏ tick phó bản trong lúc đang chạy. Chỉ cập nhật danh sách, không dừng phó bản đang làm.
     *   on=lt,tq,...  mọi phó bản đang tick (kể cả chưa tới giờ / chưa đủ điều kiện)
     * Phó bản đang làm (cả lúc về thành giữa 2 lượt) vẫn tick: làm tiếp tới hết lượt rồi mới chọn tiếp theo thứ tự danh sách.
     * Bỏ tick đúng phó bản đang làm: thoát phó bản đó (lượt tính thất bại), rồi sang phó bản kế.
     */
    private function pbList(kv:Object):String {
        if (_state != PB) return pbStart(kv);
        var queue:Array = pbParseQueue(kv);
        var on:Array = String(kv.on || kv.list || "").split(",");
        var cur:String = _pbRun ? _pbRun.key : _pbCur;
        if (cur && on.indexOf(cur) >= 0) {
            var has:Boolean = false;
            for each (var q:Object in queue) if (q.key == cur) { has = true; break; }
            if (!has && pbCfg(cur)) queue.unshift(pbCfg(cur));      // còn tick nhưng panel tạm bỏ khỏi danh sách (giờ / điều kiện): giữ để làm tiếp
        }
        var skip:Object = {}, wait:Object = {};                       // giữ lý do bỏ qua / chờ của phó bản đã có sẵn trong danh sách cũ
        for each (var q2:Object in queue) if (pbCfg(q2.key)) {
            if (_pbSkip[q2.key]) skip[q2.key] = _pbSkip[q2.key];
            if (_pbWait[q2.key]) wait[q2.key] = _pbWait[q2.key];
        }
        _pbQueue = queue; _pbSkip = skip; _pbWait = wait;
        _pbDone = pbParseDone(kv);
        applyFightCfg(kv);                                            // kỹ năng / hỗ trợ / nhặt chung: áp dụng ngay
        var changed:Array = [];
        if (_pbRun) {                                                 // lượt đang chạy: áp dụng ngay cài đặt mới của chính phó bản này
            var nc:Object = null;
            for each (var q4:Object in queue) if (q4.key == _pbRun.key) { nc = q4; break; }
            if (nc && nc != _pbRun.cfg) {
                changed = pbCfgDiff(_pbRun.cfg, nc);
                _pbRun.cfg = nc;
            }
            if (_pbRun.cfg && _pbRun.cfg.sk) { applyFightCfg(_pbRun.cfg.sk); _castPend = []; }   // bộ kỹ năng riêng của phó bản
        }
        if (kv.live == "1") _emit("train", "pb cài đặt mới" + (_pbRun ? " (áp dụng ngay cho " + _pbRun.key + ")" : "") + (changed.length ? ": " + changed.join(", ") : "")
                                 + "; kỹ năng: " + fightCfgText());
        else _emit("train", "pb list " + _pbQueue.map(function(q3:*, i:int, a:Array):String { return q3.key; }).join(",") + (cur ? " (đang làm " + cur + ")" : ""));
        if (cur && on.indexOf(cur) < 0) {                             // bỏ tick đúng phó bản đang làm: thoát
            _pbCur = null;
            if (_pbRun && (_pbPhase == "in" || _pbPhase == "dead")) {
                _pbRun.unpick = true;                                 // pbInStep thoát ở nhịp kế (chết thì sau khi hồi sinh)
            } else if (!_pbRun && (_pbPhase == "open" || _pbPhase == "enter")) {
                _pbPhase = "town"; _pbAt = getTimer(); _pbTry = 0;
            }
            _emit("train", "pb unpick " + cur);
        }
        if (_pbQueue.length == 0 && !_pbRun) { _emit("train", "pb finish"); pbStop("không còn phó bản được chọn"); return "ok pb idle"; }
        return "ok pb " + _pbPhase;
    }

    /** các mục cài đặt phó bản khác nhau (để log); tuyến chỉ báo "tuyến" */
    private static function pbCfgDiff(a:Object, b:Object):Array {
        var out:Array = [];
        for (var k:String in b) {
            if (k == "key" || k == "lzc") continue;
            var x:* = a[k], y:* = b[k];
            if (y is Array || y is Object && !(y is String) && !(y is Number) && !(y is Boolean)) {
                var sx:String = "", sy:String = "";
                try { sx = JSON.stringify(x); sy = JSON.stringify(y); } catch (e:Error) { }
                if (sx != sy) out.push(k == "sk" ? "bộ kỹ năng" : k);
            } else if (x !== y) out.push(k + " " + x + "→" + y);
        }
        return out;
    }

    private function pbStop(why:String):void {
        if (_state == PB) { _state = IDLE; _timer.stop(); }
        _pbCur = null;
        _fast = false; _pbNoBoss = false;
        afkKill("dừng phó bản");
        ptRestore();
        _pbRun = null; _pbPhase = ""; _target = null; _pickBag = null;
        _emit("train", "pb stop " + why);
    }

    private function pbStatus():String {
        if (_state != PB && !_pbRun) return "";
        var s:String = " pb=" + (_pbRun ? _pbRun.key : "-") + " pbphase=" + _pbPhase;
        if (_pbRun && _pbRun.cfg) s += " pbfloor=" + (_pbRun.key == "lt" ? "x" + lzCount() : _pbRun.key == "mc" && curMap() == MC_SECRET ? "thanbi" : (_pbRun.floor + 1) + "/" + (_pbRun.key == "mc" ? MC_ROOMS.length : PB_DEF[_pbRun.key].maps.length)) + " pbdeaths=" + _pbRun.deaths;
        var sk:Array = [];
        for (var k:String in _pbSkip) sk.push(k + ":" + enc(_pbSkip[k]));
        if (sk.length) s += " pbskip=" + sk.join(",");
        s += " roses=" + roses();
        return s;
    }

    private function pbSkipKey(key:String, why:String):void {
        _pbSkip[key] = why;
        _emit("train", "pb skip " + key + " " + why);
    }

    private function pbCfg(key:String):Object {
        for each (var q:Object in _pbQueue) if (q.key == key) return q;
        return null;
    }

    private function pbKeyOfMap(m:int):String {
        for (var k:String in PB_DEF) if ((PB_DEF[k].maps as Array).indexOf(m) >= 0) return k;
        return null;
    }

    /** tổng hoa hồng trong túi (6 loại, như bảng hồi sinh của game) */
    private function roses():int {
        var n:int = 0;
        try { for each (var g:Object in gi().mainCharData.goodsInfo.goodsBagArr) if (ROSE_IDS.indexOf(int(g.id)) >= 0) n += int(g.count); } catch (e:Error) { }
        return n;
    }

    private function pbHook():void {
        if (_pbHooked) return;
        try {
            if (!_pbTeam && _c.Team_MsgSenderProxy) { var T:Class = _c.Team_MsgSenderProxy as Class; _pbTeam = new T(); }
            _c.NetWorkManager.registerMsg(10712, onPb10712, "VlcmPB_10712");
            _c.NetWorkManager.registerMsg(10726, function(n:Object):void { _pbDoneAt = getTimer(); }, "VlcmPB_10726");
            _c.NetWorkManager.registerMsg(10300, onPb10300, "VlcmPB_10300");
            _c.NetWorkManager.registerMsg(10052, onPb10052, "VlcmPB_10052");
            _pbHooked = true;
            lzHook();
        } catch (e:Error) { _emit("train", "error pb hook " + e.message); }
    }

    private function onPb10712(n:Object):void {
        var b:ByteArray = n.body as ByteArray;
        try {
            b.position = 4;
            var list:Object = {};
            var cnt:int = b.readShort();
            for (var i:int = 0; i < cnt; i++) {
                b.readInt();                                   // id phó bản
                var map:int = b.readInt();
                var enter:int = b.readShort(), total:int = b.readShort();
                b.readShort(); b.readShort(); b.readByte();
                list[map] = { enter: enter, total: total };
            }
            _pbList = list; _pbListAt = getTimer();
        } catch (e:Error) { _emit("train", "warn đọc 10712 lỗi: " + e.message); }
        finally { b.position = 4; }
    }

    private function onPb10300(n:Object):void {
        var b:ByteArray = n.body as ByteArray;
        try { b.position = 4; var m:int = b.readInt(); b.readInt(); if (b.readByte() == 1 && m == curMap()) _pbGateAt = getTimer(); }
        catch (e:Error) { } finally { b.position = 4; }
    }

    private function onPb10052(n:Object):void {
        var b:ByteArray = n.body as ByteArray;
        try { b.position = 4; _pbAsk = { x: b.readShort(), y: b.readShort(), msg: b.readUTF(), at: getTimer(), map: curMap() }; }
        catch (e:Error) { } finally { b.position = 4; }
    }

    private function pbBeginRun(key:String, resumed:Boolean):void {
        var cfg:Object = pbCfg(key);
        _pbRun = { key: key, cfg: cfg, deaths: 0, start: getTimer(), floor: 0, floorMap: -1, floorAt: getTimer(),
                   ri: 0, lapFound: false, boss: false, quietAt: -1, noTarget: 0, progressAt: getTimer(), moveAt: 0, dwell: 0, exitWalkAt: 0,
                   mobSeenAt: getTimer(), lzMax: 0, lzBreaks: 0, lzGapMax: 0, lzRescues: 0, okWhy: "" };
        _pbPhase = "in"; _pbAt = getTimer(); _pbCur = key;
        _pbDoneAt = -1; _pbGateAt = -1; _pbAsk = null;
        ppReset(); _ap = null;
        if (cfg && cfg.sk) {                                             // bộ kỹ năng riêng của phó bản này
            applyFightCfg(cfg.sk); _castPend = []; _skLock = {}; _pubUntil = {};
            _emit("train", "pb " + key + " dùng nhóm kỹ năng " + cfg.sk.setNo + ": " + fightCfgText());
        }
        _emit("train", "pb run " + key + (resumed ? " (chạy tiếp lượt đang dở)" : ""));
    }

    private function pbEndRun(ok:Boolean, why:String):void {
        if (!_pbRun) return;
        if (_pbRun.key == "pt") ptRestore();
        if (_pbRun.key == "lt") _emit("train", "pb lt thống kê: liên trảm cao nhất " + _pbRun.lzMax + ", đứt chuỗi " + _pbRun.lzBreaks + " lần, lâu nhất giữa 2 lần giết "
                                      + (_pbRun.lzGapMax / 1000).toFixed(1) + "s, cứu chuỗi " + _pbRun.lzRescues + " lần");
        _fast = false; _pbNoBoss = false;
        afkKill("hết lượt");
        ppReset(); _ap = null;
        var key:String = _pbRun.key;
        if (ok) _pbDone[key] = int(_pbDone[key]) + 1;
        _emit("train", "pb end " + key + " " + (ok ? "ok" : "fail") + " " + why);
        _pbRun = null;
        _target = null; _pickBag = null;
        _pbPhase = "town"; _pbAt = getTimer(); _pbTry = 0; _pbList = null;
    }

    /** phó bản kế tiếp còn chạy được; null = hết */
    private function pbNextKey():String {
        if (_pbCur) {                                                 // phó bản đang làm: làm tiếp tới khi hết lượt / bị bỏ qua / bỏ tick
            var cq:Object = pbCfg(_pbCur);
            if (cq && pbKeyOk(cq)) return cq.key;
            _pbCur = null;
        }
        for each (var q:Object in _pbQueue) if (pbKeyOk(q)) return q.key;
        return null;
    }
    private function pbKeyOk(q:Object):Boolean {
        if (_pbSkip[q.key]) return false;
        if (q.runs > 0 && int(_pbDone[q.key]) >= q.runs) return false;
        if (_pbWait[q.key]) return false;
        var why:String = lzCondFail(q.lzc);
        if (why) { _pbWait[q.key] = true; _emit("train", "pb wait " + q.key + " " + why); return false; }
        return true;
    }

    private function pbStep(now:int):void {
        // ---- chết
        if (isDead(me())) {
            if (_pbPhase != "dead") {
                afkKill("nhân vật chết");
                _pbPhase = "dead"; _deadAt = now; _reviveTries = 0; _deaths++; _target = null; _pickBag = null;
                if (_pbRun) _pbRun.deaths++;
            }
            if (_reviveTries >= REVIVE_MAX || now - _deadAt < REVIVE_FIRST + _reviveTries * REVIVE_RETRY) return;
            var local:Boolean = _pbRun != null && _pbRun.deaths <= _pbRun.cfg.rev && roses() > 0;
            pbRevive(local);
            _reviveTries++;
            return;
        }
        if (_pbPhase == "dead") {
            _deadAt = 0;
            if (_pbRun && pbKeyOfMap(curMap()) == _pbRun.key) { _pbPhase = "in"; _pbRun.floorAt = now; _pbRun.quietAt = -1; }
            else if (_pbRun) pbEndRun(false, "chết " + _pbRun.deaths + " lần, đã về thành");
            else _pbPhase = "town";
            return;
        }
        pbConfirmDialogs(now);
        switch (_pbPhase) {
            case "town":   pbTownStep(now); break;
            case "open":   pbOpenStep(now); break;
            case "enter":  pbEnterStep(now); break;
            case "in":     pbInStep(now); break;
            case "reward": pbRewardStep(now); break;
            case "exit":   pbExitStep(now); break;
        }
    }

    // ---- ở thành: đi tới NPC phó bản
    private function pbTownStep(now:int):void {
        var key:String = pbKeyOfMap(curMap());
        if (key && pbCfg(key)) { pbBeginRun(key, true); return; }
        if (!pbNextKey()) { _emit("train", "pb finish"); pbStop("đã đi hết phó bản"); return; }
        if (curMap() != PB_TOWN || tileDist(me(), PB_NPC_X, PB_NPC_Y) > 5) {
            if (curMap() != PB_TOWN && _util.teleTo(PB_TOWN, now)) return;
            if (now - _lastWalk >= WALK_RETRY && me().getStatus() != "walk") { _c.MainCharSeachPathManager.mainCharWalk(PB_NPC); _lastWalk = now; }
            if (now - _pbAt > 240000) { _pbAt = now; _emit("train", "warn pb 4 phút chưa tới được NPC phó bản, thử lại"); }
            return;
        }
        if (me().getStatus() == "walk") return;
        _pbPhase = "open"; _pbAt = 0; _pbTry = 0; _pbList = null;
    }

    // ---- mở bảng phó bản của NPC, đọc số lượt
    private function pbOpenStep(now:int):void {
        if (_pbList == null) {
            if (_pbAt == 0 || now - _pbAt > 4000) {
                if (++_pbTry > 4) { _emit("train", "warn pb không mở được bảng phó bản, đi lại tới NPC"); _pbPhase = "town"; _pbAt = now; _lastWalk = 0;
                    walkTo(PB_TOWN, PB_NPC_X + 5, PB_NPC_Y + 5); return; }
                try {
                    var npc:Object = gi().scene.getCharByID(PB_NPC, 6);
                    if (npc) gi().lockOnChar = npc;
                    _pbTeam.send_10711(PB_NPC);
                } catch (e:Error) { _emit("train", "error pb 10711 " + e.message); }
                _pbAt = now;
            }
            return;
        }
        if (now - _pbListAt < 3000) return;                            // chờ 3 giây sau khi mở bảng phó bản rồi mới vào
        closeInstancePanel();
        var key:String = pbNextKey();
        if (!key) { _pbPhase = "town"; return; }
        var def:Object = PB_DEF[key], cfg:Object = pbCfg(key);
        var info:Object = null, first:int = -1;
        for each (var m:int in def.maps) if (_pbList[m]) { info = _pbList[m]; first = m; break; }
        if (!info) { pbSkipKey(key, "không có trong bảng phó bản của NPC"); return; }
        _emit("train", "pb count " + key + " " + info.enter + " " + info.total);
        if (info.total - info.enter <= 0) { pbSkipKey(key, "hết lượt hôm nay"); return; }
        var r:int = roses(), need:int = key == "tq" ? Math.max(cfg.minr, cfg.rev) : cfg.rev;
        if (r < need) {
            // túi đồ có thể chưa tải xong lúc mới vào game: đếm lại sau 3 giây, tối đa 3 lần rồi mới bỏ qua
            if (_pbRoseTry < 3) { if (now < _pbRoseWait) return; _pbRoseTry++; _pbRoseWait = now + 3000; if (_pbRoseTry == 1) _emit("train", "info pb " + key + " đếm được " + r + " hoa (cần " + need + "), chờ túi đồ tải xong rồi đếm lại"); return; }
            _pbRoseTry = 0; pbSkipKey(key, "thiếu hoa hồng (" + r + "/" + need + ")"); return;
        }
        _pbRoseTry = 0;
        try { _pbTeam.send_10701([PB_NPC, first, 1, 1]); }              // 1 = thưởng thường, 1 = độ khó thấp nhất
        catch (e2:Error) { _emit("train", "error pb 10701 " + e2.message); }
        _emit("train", "pb enter " + key + " (còn " + (info.total - info.enter) + "/" + info.total + " lượt, hoa " + r + ")");
        _pbPhase = "enter"; _pbAt = now; _pbEnterKey = key;
    }

    private function pbEnterStep(now:int):void {
        var key:String = _pbEnterKey;
        if (key && pbKeyOfMap(curMap()) == key) { pbBeginRun(key, false); return; }
        if (now - _pbAt > 20000) {
            _pbTry++;
            if (_pbTry >= 3) { pbSkipKey(key, "không vào được"); _pbTry = 0; }
            _pbPhase = "town"; _pbAt = now;
        }
    }

    // ---- trong phó bản
    private function pbInStep(now:int):void {
        var run:Object = _pbRun, def:Object = PB_DEF[run.key];
        var here:int = curMap();
        if (pbKeyOfMap(here) != run.key) {
            if (here == PB_TOWN) pbEndRun(_pbDoneAt > 0, _pbDoneAt > 0 ? "đã về thành" : "bị đưa ra khỏi phó bản");
            return;
        }
        if (_pbDoneAt > 0) { _pbPhase = "reward"; _pbAt = now; _target = null; return; }
        if (run.unpick) { pbExit("bỏ tick phó bản đang chạy"); return; }
        _fast = false; _pbNoBoss = false;
        if (anyMob()) run.mobSeenAt = now;
        if (run.cfg.nomob > 0 && now - run.mobSeenAt > run.cfg.nomob * 60000) { pbExit("không có quái trong " + run.cfg.nomob + " phút"); return; }
        var fl:int = (def.maps as Array).indexOf(here);
        if (run.key == "mc") {
            if (now - run.progressAt > PB_FLOOR_STUCK) pbUnstick(run, now, "mê cung phòng " + (MC_ROOMS.indexOf(here) + 1));
            mcStep(run, now); return;
        }
        if (fl != run.floor || here != run.floorMap) {                  // vào tầng / ải mới
            run.floor = fl; run.floorMap = here; run.floorAt = now; run.ri = 0; run.quietAt = -1; run.noTarget = 0;
            run.lapFound = false; run.lapEmpty = false; run.boss = false; run.moveAt = 0; run.progressAt = now; _pbGateAt = -1;
            run.mouse = false; run.mouseDone = false; run.mouseTarget = null; run.mobSeenAt = now;
            run.noTgtAt = -1; run.goingGate = false; run.atGate = 0; run.s51At = 0; run.gateLogged = false; run.afkTried = false;
            _target = null; _ap = null; _pp = null; _black = {}; _blackN = {};
            _emit("train", "pb floor " + run.key + " " + (fl + 1) + "/" + def.maps.length + " map " + here);
            if (run.cfg.sf > 0 && fl >= run.cfg.sf) { pbExit("đã qua ải " + run.cfg.sf + ", dừng phó bản theo cài đặt", true); return; }
        }
        if (now - run.floorAt > PB_FLOOR_STUCK && now - run.progressAt > PB_FLOOR_STUCK) pbUnstick(run, now, "tầng " + (fl + 1));
        if (run.key == "pt") { ptStep(run, now); return; }
        if (run.key == "lt") { ltStep(run, now); return; }
        if (pbMouseStep(run, now, run.cfg.route)) return;             // ải chuột (vd. Doanh Trại ải 5)
        var next:int = fl + 1 < def.maps.length ? def.maps[fl + 1] : -1;
        // treo máy của game (Thiên Quan): đang treo thì tool chỉ theo dõi
        if (_afk) { afkStep(run, now, next); return; }
        // tâm = nhân vật; phạm vi tìm quái theo cài đặt từng phó bản (mặc định 99)
        var range:int = run.cfg.range > 0 ? run.cfg.range : 99;
        _map = here; _x = me().tile_x; _y = me().tile_y; _r = range;
        if (support(now)) return;
        // nhặt trước (đồ để lâu sẽ mất): có quái trong 20 ô thì nhặt trong 20 ô, không thì toàn map. Đang đi sang tầng thì thôi nhặt.
        if (!run.goingGate && pbPickStep(now, mobWithin(PB_R) ? PB_R : 9999)) { run.progressAt = now; run.noTgtAt = -1; return; }
        // giữ mục tiêu tới khi chết hoặc cách > 12 ô; chọn mới: con gần nhất trong phạm vi
        // (con ở xa mới chọn thì cứ đi lại gần; chỉ bỏ khi đã từng ở gần <= 12 ô rồi bị kéo ra xa hơn 12 ô)
        if (_target != null) {
            var td:Number = tileDist(me(), _target.tile_x, _target.tile_y);
            if (td <= PB_HOLD) run.holdNear = _target.id;
            if (isDead(_target) || !_target.usable || (run.holdNear == _target.id && td > PB_HOLD && !isBossMob(_target))) {
                if (isDead(_target)) { _kills++; _util.perfKill(); noteKill(_target, now); }
                _target = null; _ap = null;
            }
        }
        if (_target == null && dropWaiting(now)) { run.progressAt = now; run.noTgtAt = -1; return; }   // chờ đồ rơi rồi nhặt
        // đang đánh boss (vừa đi nhặt đồ): quay lại đúng con boss đó
        var boss:Object = run.bossObj != null && mobRefOk(run.bossObj, run.bossId) && !isDead(run.bossObj) ? run.bossObj : null;
        if (_target == null && boss != null) { _target = boss; _targetSince = now; _targetHp = mobHp(boss); gi().lockOnChar = boss; }
        var mob:Object = _target != null ? _target : pick(now);
        if (mob && isBossMob(mob)) {
            run.bossObj = mob; run.bossId = int(mob.id);
            bossBuffLog(mob);
            // "bỏ qua đánh boss ở trạng thái đếm số": boss mang buff bất tử / bảo hộ (game nhận biết: id 80001,80004,4001,4004,4011 hoặc loại 172)
            if (run.cfg.bosscount && bossProtected(mob)) {
                if (!run.bcLogged) { run.bcLogged = true; _emit("train", "pb " + run.key + " boss đang ở trạng thái bất tử / đếm số: tạm không đánh boss, đánh quái quanh và chờ"); }
                var adds:Object = ltPick(now, 8, false);                     // quái thường quanh đó (không phải boss)
                run.progressAt = now; run.noTgtAt = -1;
                if (adds) { _target = adds; _targetSince = now; _targetHp = mobHp(adds); gi().lockOnChar = adds; fight(now); return; }
                _target = null; gi().lockOnChar = null;
                if (tileDist(me(), mob.tile_x, mob.tile_y) > 5) moveTo(curMap(), mob.tile_x, mob.tile_y, 100);
                return;
            }
            if (run.bcLogged) { run.bcLogged = false; _emit("train", "pb " + run.key + " boss hết trạng thái bất tử: đánh tiếp"); }
            if (_target != mob) { _target = mob; _targetSince = now; _targetHp = mobHp(mob); gi().lockOnChar = mob; }
        }
        if (mob) {
            run.noTgtAt = -1; run.noTarget = 0; run.lapFound = true; run.progressAt = now; run.goingGate = false;
            if (_target == null && run.patrolling) {                     // vừa thấy quái lúc đang đi tuần: hủy đường tuần, quay sang đánh
                run.patrolling = false; run.gp = null;
                try { _c.MainCharSeachPathManager.clear(); me().stopMove(); } catch (e3:Error) { }
                _mv = null;
            }
            if (run.cfg.jump && pbJump(mob, now, 0)) return;   // Doanh Trại: nhảy quanh quái ở mọi ải game cho nhảy (xen kẽ với đánh)
            fight(now);
            return;
        }
        _target = null;
        if (run.noTgtAt < 0) run.noTgtAt = now;
        if (now - run.noTgtAt < PB_QUIET) return;                       // không có mục tiêu liên tục 1 giây
        // sang tầng: chỉ khi cổng sang tầng kế ĐÃ MỞ (10300 đúng map đang đứng / cổng trong game opened) và hết túi chưa bị cấm
        if (next > 0 && pbPortalOpen(next)) {
            if (run.goingGate || !pbBagsPending(now)) { pbGoMap(next, now); return; }
        }
        if (next > 0 && run.lapEmpty && !run.gateLogged) { run.gateLogged = true; _emit("train", "pb " + run.key + " đã đi hết vòng tuần, chưa thấy cổng tầng kế mở — tiếp tục tìm quái"); }
        // Thiên Quan: hết 1 vòng tuần không thấy quái mà cổng chưa mở -> bật treo máy của game, phạm vi 99
        if (run.key == "tq" && run.cfg.afk && run.lapEmpty && !run.afkTried) { run.afkTried = true; afkStart(run, now); return; }
        pbPatrol(run, now);
    }




    // ---- canh kẹt trạng thái "đang đi": status walk mà 2s không đổi ô -> dừng như game, xóa đường cũ, bước lệch 1 ô
    private var _wwX:int = -1, _wwY:int = -1, _wwAt:int = 0, _wwCount:int = 0, _wwLogAt:int = -60000;
    private function walkWatch(now:int):void {
        var m:Object = me();
        if (m.getStatus() != "walk" || isDead(m) || busyAnim(m)) { _wwAt = now; _wwX = m.tile_x; _wwY = m.tile_y; return; }
        if (m.tile_x != _wwX || m.tile_y != _wwY) { _wwAt = now; _wwX = m.tile_x; _wwY = m.tile_y; return; }
        if (now - _wwAt < 1000) return;
        _wwAt = now; _wwCount++;
        try { _c.MainCharSeachPathManager.clear(); } catch (e:Error) { }
        try { m.stopMove(); } catch (e2:Error) { }
        _lastWalkCmd = -10000;                                             // gỡ kẹt: được gửi ngay
        walkTo(curMap(), m.tile_x + (Math.random() < 0.5 ? -1 : 1), m.tile_y + (Math.random() < 0.5 ? -1 : 1), "gỡ kẹt");
        if (_pbRun) { _pbRun.moveAt = 0; if (_pbRun.gp) _pbRun.gp.prog = now; }
        _ap = null;
        if (now - _wwLogAt > 30000) { _wwLogAt = now; _emit("train", "info kẹt trạng thái đi (đứng yên 1 giây) — đã dừng và bước lệch 1 ô (lần " + _wwCount + ")"); }
    }

    /**
     * Đi tới một điểm, phát hiện điểm không tới được: đã gửi lệnh đi mà 1,5s không lại gần thêm >= 1 ô -> "stuck" (nhớ theo tầng, log 1 lần).
     * Trả về "arrived" | "walking" | "stuck".
     */
    /** đang diễn chiêu / nhảy / bị khống chế: đứng yên là bình thường, không tính là kẹt */
    private function busyAnim(m:Object):Boolean {
        try {
            var st:String = m.getStatus();
            if (st == "attack" || st == "jump") return true;
            if (m.isJumping && (m.isJumping() || m.on2Jumping() || m.on3Jumping())) return true;
        } catch (e:Error) { }
        return charFixed();
    }
    private function goPoint(run:Object, x:int, y:int, now:int, arrive:Number, retry:int):String {
        var d:Number = tileDist(me(), x, y);
        if (d <= arrive) { run.gp = null; return "arrived"; }
        var g:Object = run.gp;
        if (g == null || g.x != x || g.y != y || now - g.last > 1000) { g = run.gp = { x: x, y: y, best: d, prog: now, last: now, sent: 0 }; }
        g.last = now;
        if (d < g.best - 0.99 || busyAnim(me())) { if (d < g.best) g.best = d; if (now > g.prog) g.prog = now; }   // đang diễn chiêu / nhảy: không tính giờ
        // không tới được: đã gửi lệnh đi (ít nhất 1,5s trước) mà 2,5s không lại gần
        if (now - g.prog > 2500 && g.sent > 0 && now - g.sent > 1500) {
            run.gp = null;
            var k:String = curMap() + ":" + x + ":" + y;
            if (!run.badPts) run.badPts = {};
            if (!run.badPts[k]) { run.badPts[k] = true; _emit("train", "info điểm (" + x + "," + y + ") map " + curMap() + " không tới được, bỏ qua"); }
            return "stuck";
        }
        if (me().getStatus() != "walk" && now - int(run.moveAt) > retry) {
            if (moveTo(curMap(), x, y, 0, "đi tuần")) { run.moveAt = now; g.sent = now; run.progressAt = now; }
        }
        return "walking";
    }
    private function badPoint(run:Object, p:Array):Boolean { return run.badPts && run.badPts[curMap() + ":" + p[0] + ":" + p[1]]; }
    /** điểm tuần kế tiếp còn tới được (bỏ qua điểm hỏng); null nếu tất cả đều hỏng */
    private function nextGoodPoint(run:Object, route:Array, idxKey:String):Array {
        for (var i:int = 0; i < route.length; i++) {
            var p:Array = route[int(run[idxKey]) % route.length];
            if (!badPoint(run, p)) return p;
            run[idxKey] = int(run[idxKey]) + 1;
        }
        return null;
    }


    /** boss đang mang buff bất tử / bảo hộ — đúng cách game nhận biết (BuffInfo.isProtectOrInvincibleBuff) */
    private static function bossProtected(c:Object):Boolean {
        try {
            for each (var b:Object in c.data.buffInfo.buffArr) {
                if (!b) continue;
                var id:int = int(b.id);
                if (id == 80001 || id == 80004 || id == 4001 || id == 4004 || id == 4011) return true;
                if (b.res && int(b.res.type) == 172) return true;
            }
        } catch (e:Error) { }
        return false;
    }
    /** ghi 1 lần mỗi loại buff boss mang (để nhận ra "trạng thái đếm số" thật trên game) */
    private var _bossBuffSeen:Object = {};
    private function bossBuffLog(c:Object):void {
        try {
            for each (var b:Object in c.data.buffInfo.buffArr) {
                if (!b) continue;
                var id:int = int(b.id);
                if (_bossBuffSeen[id]) continue;
                _bossBuffSeen[id] = true;
                _emit("train", "info boss mang buff \"" + (b.res ? b.res.name : "?") + "\" id " + id + " loại " + (b.res ? b.res.type : "?") + (bossProtected({ data: { buffInfo: { buffArr: [b] } } }) ? " (bất tử/bảo hộ)" : ""));
            }
        } catch (e:Error) { }
    }

    // ---- nhặt đồ trong phó bản: nhặt trước, đánh sau; không đi được thì đánh trong lúc chờ; cấm theo bậc để không kẹt tầng
    private static const PP_MAX:int = 4000;       // nhắm một túi tối đa 4s
    private static const PP_BAN:int = 10000;      // cấm tạm 10s
    private static const PP_GAP:int = 200;        // 2 lệnh nhặt cách nhau >= 200ms
    private var _pp:Object = null;                // túi đang nhặt
    private var _ppBan:Object = {};               // id -> mốc hết cấm (-1 = cấm hẳn trong lượt)
    private var _ppBanN:Object = {};              // id -> số lần đã cấm tạm
    private static function isRose(info:Object):Boolean {
        if (!info) return false;
        var g:int = int(info.gid);
        if (g >= 1201 && g <= 1206) return true;
        return String(info.name).toLowerCase().indexOf("hoa hồng") == 0;
    }
    /** túi nên nhặt: hoa hồng > đồng > theo luật nhặt > gần hơn; túi đầy thì chỉ hoa hồng + đồng; bỏ túi đang bị cấm */
    private function pbChooseBag(now:int, maxD:Number):Object {
        var full:Boolean = false;
        try { full = _util.freeSlots() <= 0; } catch (e:Error) { }
        var best:Object = null, bp:int = int.MAX_VALUE, bd:Number = Number.MAX_VALUE;
        try {
            for each (var c:Object in gi().scene.getCharsByType(11)) {
                if (!c.usable) continue;
                var ban:* = _ppBan[c.id];
                if (ban != undefined && (ban < 0 || ban > now)) continue;
                var d:Number = tileDist(me(), c.tile_x, c.tile_y);
                if (d > maxD) continue;
                var info:Object = _drops[c.id], p:int;
                if (isRose(info)) p = 0;
                else if (info && info.money) p = 1;
                else {
                    if (full || _pickMode == "off") continue;
                    var rk:int = _pickMode == "all" ? 0 : (info ? ruleRank(info) : -1);
                    if (rk < 0) continue;
                    p = 2 + rk;
                }
                if (p < bp || (p == bp && d < bd)) { best = c; bp = p; bd = d; }
            }
        } catch (e2:Error) { }
        return best;
    }
    /** còn túi chưa bị cấm cần nhặt (toàn map) */
    private function pbBagsPending(now:int):Boolean { return pbChooseBag(now, 9999) != null; }
    private function mobWithin(r:Number):Boolean {
        try { for each (var c:Object in gi().scene.getCharsByType(2)) if (c.usable && !isDead(c) && tileDist(me(), c.tile_x, c.tile_y) <= r) return true; } catch (e:Error) { }
        return false;
    }
    private function charFixed():Boolean {
        try { var d:Object = gi().mainCharData; return d.isFixed == true || d.isSleep == true; } catch (e:Error) { }
        return false;
    }
    private function ppReset():void { _pp = null; _ppBan = {}; _ppBanN = {}; }
    /** true = đang nhặt (nhịp này không làm gì khác); false = không có túi / tạm không đi được (cho đánh quái) */
    private function pbPickStep(now:int, maxD:Number):Boolean {
        if (_pp != null && !bagAlive(_pp.bag)) {
            _picked++;
            if (_pp.info && _pp.info.at) _util.perfPick(now - _pp.info.at);
            if (_pp.info && _pp.info.money) _util.expectMoney(int(_pp.info.amt), _pp.copper, _pp.name);
            else _emit("train", "pick ok " + _pp.name);
            _pp = null;
        }
        if (_pp == null) {
            var bag:Object = pbChooseBag(now, maxD);
            if (bag == null) return false;
            var inf:Object = _drops[bag.id];
            _pp = { bag: bag, since: now, walkAt: 0, sendAt: 0, wx: -999, wy: -999, pause: 0, info: inf, copper: _util.copper(),
                    name: inf && inf.name ? inf.name : "#" + bag.id, note: "" };
            if (_target != null && isDead(_target)) { _kills++; _util.perfKill(); }
            _target = null; _ap = null;
            gi().lockOnChar = null;
        }
        var b:Object = _pp.bag, m:Object = me();
        if (_pp.sendAt > 0) {
            var nt:String = _util.pickNotice(_pp.sendAt - 50, now);
            if (nt) _pp.note = nt == "far" ? "server báo quá xa" : nt == "others" ? "của người khác" : nt;
            if (nt == "others") { _pickRefused++; _ppBan[b.id] = now + PICK_OTHERS; _pp = null; return false; }
            if (nt == "cant" || nt == "full") { ppGiveUp(now, true, nt == "full" ? "túi đầy" : "server báo không thể nhặt"); return false; }
            if (nt == "far") { _pp.walkAt = 0; _pp.far = true; _pp.sendAt = 0; }     // server báo quá xa: đi vào đúng ô túi; sát rồi vẫn xa thì bấm Space
        }
        if (now - _pp.since > PP_MAX) { ppGiveUp(now, false, "quá " + int(PP_MAX / 1000) + " giây chưa nhặt được"); return false; }
        if (now < _pp.pause) return false;                                   // tạm không đi được: cho đánh quái gần
        if (charFixed()) { _pp.pause = now + 500; _pp.note = "bị khống chế (định thân / ngủ)"; return false; }
        var st:String = m.getStatus();
        if (st == "attack" && now - _pp.since < PICK_ANIM) return true;     // chờ diễn xong chiêu
        var md:int = Math.abs(m.tile_x - b.tile_x) + Math.abs(m.tile_y - b.tile_y);
        if (md == 0) {                                                       // đứng đúng ô túi như game rồi mới nhặt
            if (_pp.far && spacePick(now)) return true;
            if (now - _pp.sendAt >= PP_GAP) { pipe("PICK_UP", b.id); _pp.sendAt = now; }
            return true;
        }
        if (md <= 1 && (_pp.far || _pp.stalled) && spacePick(now)) return true;   // sát túi mà không vào được ô / vẫn bị báo xa: phím Space của game
        if (_pp.walkAt > 0 && st != "walk" && now - _pp.walkAt >= 1000 && m.tile_x == _pp.wx && m.tile_y == _pp.wy) {
            _pp.stalled = true;
            if (md <= 1) return true;
            _pp.pause = now + 1000; _pp.walkAt = 0; _pp.note = "gửi lệnh đi 1 giây không nhích được";
            return false;
        }
        if (_pp.walkAt == 0 || (st != "walk" && now - _pp.walkAt >= 500 && (m.tile_x != _pp.wx || m.tile_y != _pp.wy))) {
            var id:int = b.id, self:VlcmTrain = this;
            var spot:String = curMap() + "," + b.tile_x + "," + b.tile_y + ",0";
            try {
                var MCB:Class = _c.MoveCallBack as Class;
                var cb:Object = MCB ? new MCB() : null;
                if (cb) {
                    cb.onMoveArrived = function(sc:Object = null, tile:Object = null):void {      // tới đúng ô mới nhặt
                        try { if (self._pp && self._pp.bag.id == id && self.bag2ok(id) && self.onBagTile(id) && getTimer() - self._pp.sendAt >= PP_GAP) { self.pipe("PICK_UP", id); self._pp.sendAt = getTimer(); } } catch (e:Error) { }
                    };
                    cb.onMoveUnable = function(sc:Object = null, tile:Object = null):void {       // không đi tới được: không nhặt bừa
                        try { if (self._pp && self._pp.bag.id == id) { self._pp.stalled = true; self._pp.note = "game báo không đi tới được ô túi"; } } catch (e:Error) { }
                    };
                }
                if (!walkCmd(spot, cb, "nhặt", b.tile_x, b.tile_y, false)) return true;     // vừa gửi lệnh đi khác: nhịp sau
                _mv = { x: b.tile_x, y: b.tile_y, stop: 0, at: now, fx: m.tile_x, fy: m.tile_y, map: curMap(), diag: false };
            } catch (e2:Error) { }
            _pp.walkAt = now; _pp.wx = m.tile_x; _pp.wy = m.tile_y;
        }
        return true;
    }
    private function onBagTile(id:int):Boolean {
        try { var c:Object = gi().scene.getCharByID(id, 11); return c != null && c.tile_x == me().tile_x && c.tile_y == me().tile_y; } catch (e:Error) { }
        return false;
    }
    /** nhặt bằng phím Space của game (PickUp_AutoPickUpCommand: nhặt quanh mình), như người chơi bấm Space; tối đa 1 lần / 0,45s */
    private var _spaceAt:int = -10000;
    private function spacePick(now:int):Boolean {
        try {
            if (_c.GameState && _c.GameState.SPACE_TO_SKILL) return false;           // Space đã đặt thành dùng skill
            if (gi().mainCharData.afkInfo2 && gi().mainCharData.afkInfo2.isStart) return false;
        } catch (e:Error) { }
        if (now - _spaceAt < 450) return true;
        _spaceAt = now;
        pipe("KEY_SPACE");
        if (_pp) _pp.sendAt = now;
        return true;
    }
    private function bag2ok(id:int):Boolean {
        try { var c:Object = gi().scene.getCharByID(id, 11); return c != null && c.usable; } catch (e:Error) { }
        return false;
    }
    private function ppGiveUp(now:int, permanent:Boolean, why:String):void {
        var b:Object = _pp.bag, id:int = b.id;
        var n:int = int(_ppBanN[id]) + 1;
        _ppBanN[id] = n;
        var perm:Boolean = permanent || n >= 3;
        _ppBan[id] = perm ? -1 : now + PP_BAN;
        var d:Number = tileDist(me(), b.tile_x, b.tile_y);
        _emit("train", "pick bỏ " + _pp.name + (perm ? " (bỏ hẳn trong lượt)" : " 10 giây (lần " + n + ")") + ": " + why
              + "; trạng thái=" + me().getStatus() + ", khống chế=" + (charFixed() ? "có" : "không") + ", cách " + d.toFixed(1) + " ô"
              + (_pp.note ? ", " + _pp.note : ""));
        if (!permanent) walkTo(curMap(), me().tile_x + 1, me().tile_y, "lệch khi bỏ túi");   // bước lệch 1 ô
        _pp = null;
    }


    // ---- treo máy của game (Thiên Quan): hết 1 vòng tuần không thấy quái mà cổng chưa mở -> treo, phạm vi 99 (mức tối đa ô nhập của game)
    //      bật/tắt y như bảng Treo máy: lưu cài đặt 50581 (server trả 50582 -> game tự bắt đầu 50583=1), dừng 50583=0; xong trả lại phạm vi cũ
    private static const AFK_MAX:int = 3 * 60000;
    private var _afk:Object = null;            // {old, at, phase: on|stopping, stopAt, why, exit}
    private function afkInfo():Object { try { return gi().mainCharData.afkInfo2; } catch (e:Error) { } return null; }
    private function afkStart(run:Object, now:int):void {
        var ai:Object = afkInfo(), snd:Object = proxy("Afk_MsgSenderProxy");
        if (!ai || !snd) { _emit("train", "warn pb tq không bật được treo máy của game (thiếu dữ liệu)"); return; }
        _afk = { old: int(ai.afkRangeCount), at: now, phase: "on", sent1: false };
        try { ai.afkRangeCount = 99; snd.send_50581(); } catch (e:Error) { _afk = null; _emit("train", "warn pb tq bật treo máy lỗi: " + e.message); return; }
        _target = null; _ap = null; _pp = null; gi().lockOnChar = null;
        _emit("train", "pb tq đi hết vòng không thấy quái, cổng chưa mở: bật treo máy của game, phạm vi 99 (cũ " + _afk.old + "), tối đa " + int(AFK_MAX / 60000) + " phút");
    }
    private function afkStep(run:Object, now:int, next:int):void {
        var ai:Object = afkInfo(), snd:Object = proxy("Afk_MsgSenderProxy");
        run.progressAt = now;
        if (_afk.phase == "on") {
            if (!_afk.sent1 && now - _afk.at > 2500 && ai && !ai.isStart) { _afk.sent1 = true; try { snd.send_50583(1); } catch (e:Error) { } }
            var open:Boolean = next > 0 && pbPortalOpen(next);
            if (open || _pbDoneAt > 0) afkStopBegin(now, open ? "cổng đã mở" : "đã hoàn thành", false);
            else if (now - _afk.at > AFK_MAX) afkStopBegin(now, "treo " + int(AFK_MAX / 60000) + " phút vẫn chưa mở cổng — tự tìm quái tiếp", false);
            return;
        }
        // đang dừng: đợi lưu lại phạm vi cũ (server trả 50582 có thể làm game tự bật lại) rồi gửi dừng, kiểm lại một lần nữa
        if (now - _afk.stopAt >= 1500 && !_afk.stop1) { _afk.stop1 = true; try { snd.send_50583(0); } catch (e1:Error) { } }
        if (now - _afk.stopAt >= 3500) {
            if (ai && ai.isStart && !_afk.stop2) { _afk.stop2 = true; try { snd.send_50583(0); } catch (e2:Error) { } return; }
            var ex:Boolean = _afk.exit, why:String = _afk.why;
            _afk = null;
            _emit("train", "pb tq đã tắt treo máy, trả phạm vi cũ (" + why + ")");
            if (_pbRun) { _pbRun.afkTried = false; _pbRun.lapEmpty = false; }   // đi thêm một vòng tuần trống nữa thì được treo lại
            if (ex) pbExit(why);
        }
    }
    private function afkStopBegin(now:int, why:String, exit:Boolean):void {
        var ai:Object = afkInfo(), snd:Object = proxy("Afk_MsgSenderProxy");
        try { if (ai) { ai.afkRangeCount = _afk.old; snd.send_50581(); } } catch (e:Error) { }
        _afk.phase = "stopping"; _afk.stopAt = now; _afk.why = why; _afk.exit = exit;
    }
    /** tắt ngay (dừng phó bản / hết lượt / chết) */
    private function afkKill(why:String):void {
        if (!_afk) return;
        var ai:Object = afkInfo(), snd:Object = proxy("Afk_MsgSenderProxy");
        try { if (ai) { ai.afkRangeCount = _afk.old; snd.send_50581(); } } catch (e:Error) { }
        try { snd.send_50583(0); } catch (e2:Error) { }
        var old:int = _afk.old;
        _afk = null;
        _emit("train", "pb đã tắt treo máy của game (" + why + "), trả phạm vi " + old);
        if (snd) setTimeout(function():void { try { var a2:Object = afkInfo(); if (a2 && a2.isStart) snd.send_50583(0); } catch (e3:Error) { } }, 2000);
    }

    // ---- liên trảm
    private function lzHook():void {
        if (_lzHooked) return;
        try { _c.NetWorkManager.registerMsg(12001, onLz12001, "VlcmLZ_12001"); _lzHooked = true; } catch (e:Error) { }
    }
    private function onLz12001(n:Object):void {
        var b:ByteArray = n.body as ByteArray;
        try {
            b.position = 4;
            var c:int = b.readShort(), now:int = getTimer();
            var run:Object = _pbRun && _pbRun.key == "lt" ? _pbRun : null;
            if (c > 0) {
                if (run && _lzLastAt > 0 && c > _lzCount) run.lzGapMax = Math.max(int(run.lzGapMax), now - _lzLastAt);
                _lzLastAt = now;
                if (run && c > int(run.lzMax)) run.lzMax = c;
            } else {
                if (run && _lzCount > 0) { run.lzBreaks++; _emit("train", "pb lt đứt chuỗi liên trảm (đã đạt " + _lzCount + ")"); }
                _lzLastAt = -1;
            }
            _lzCount = c;
        } catch (e:Error) { } finally { b.position = 4; }
    }
    private function lzCount():int {
        try { var c:Object = gi().mainCharData.lianzhanInfo; if (c) return int(c.count); } catch (e:Error) { }
        return _lzCount;
    }
    /** thời gian giữ chuỗi hiện tại (ms) — game đọc theo mốc 12002, chuỗi càng cao càng ngắn */
    private function lzWin():int {
        try { return int(gi().mainCharData.lianzhanInfo.time); } catch (e:Error) { }
        return 0;
    }
    private static function firstNum(t:String):int {
        if (!t) return -1;
        var m:Array = t.match(/\d+/);
        return m ? int(m[0]) : -1;
    }
    /** buff liên trảm của nhân vật (id 60001–60007, OtherConst.isLianZhanBuff): mức (số trong tên buff) + thời gian còn (ms) */
    private function lzBuff():Object {
        var out:Object = { val: 0, ms: 0, name: "", id: 0 };
        try {
            for each (var b:Object in me().data.buffInfo.buffArr) {
                if (!b) continue;
                var id:int = int(b.id);
                if (id == 0) try { var gid:String = String(b.getID()); id = int(gid.substr(gid.indexOf("_") + 1)); } catch (e1:Error) { }
                if (id < 60001 || id > 60007) continue;
                var nm:String = b.res ? String(b.res.name) : "";
                var v:int = firstNum(nm);
                if (v < 0 && b.res) v = firstNum(String(b.res.desc_short));
                if (v < 0) v = firstNum(String(b.miaoShu));
                var ms:int = 0;
                try { ms = int(_c.CDFaceManager.getLosttime(b.getID())); } catch (e2:Error) { }
                out = { val: v, ms: ms, name: nm, id: id };
                break;
            }
        } catch (e:Error) { }
        var key:String = out.id + ":" + out.val;
        if (key != _lzBuffKey) {
            _lzBuffKey = key;
            if (out.id) _emit("train", "lz buff liên trảm \"" + out.name + "\" (id " + out.id + ") mức " + (out.val < 0 ? "?" : String(out.val)) + ", còn " + int(out.ms / 60000) + " phút");
            else _emit("train", "lz không có buff liên trảm");
        }
        return out;
    }
    private function lzStatus():String {
        if (me() == null) return "";
        lzHook();
        var b:Object = lzBuff();
        var s:String = " lz=" + lzCount() + " lzb=" + b.val + " lzbmin=" + int(b.ms / 60000);
        if (_pbRun && _pbRun.key == "lt") s += " lzmax=" + _pbRun.lzMax + " lzbrk=" + _pbRun.lzBreaks + " lzresc=" + _pbRun.lzRescues + " lzgap=" + int(_pbRun.lzGapMax / 100);
        return s;
    }
    /** "lt:300:0" (dưới 300 hoặc còn ít hơn 0 phút) | "ge:400:20" (từ 400 và còn từ 20 phút) */
    private static function parseLzCond(v:*):Object {
        if (!v) return null;
        var a:Array = String(v).split(":");
        if (a.length < 3 || (a[0] != "lt" && a[0] != "ge")) return null;
        return { mode: a[0], val: int(a[1]), min: int(a[2]) };
    }
    /** null = được chạy; khác null = lý do chờ */
    private function lzCondFail(c:Object):String {
        if (!c || me() == null) return null;
        var b:Object = lzBuff(), mins:int = int(b.ms / 60000);
        var cur:String = b.id ? "đang mức " + (b.val < 0 ? "?" : String(b.val)) + " còn " + mins + " phút" : "chưa có buff liên trảm";
        if (c.mode == "lt") {
            if (b.val < 0) return null;                                   // không đọc được mức: cứ chạy
            if (b.val < c.val || mins < c.min) return null;
            return "chờ liên trảm: " + cur + " (cần dưới " + c.val + " hoặc còn ít hơn " + c.min + " phút)";
        }
        if (b.id && b.val >= c.val && mins >= c.min) return null;
        if (b.id && b.val < 0) return "chờ liên trảm: không đọc được mức buff (tên buff \"" + b.name + "\")";
        return "chờ liên trảm: " + cur + " (cần từ " + c.val + " và còn từ " + c.min + " phút)";
    }
    private static function isBossMob(c:Object):Boolean {
        try { return int(c.data.res.type) == 3; } catch (e:Error) { }
        return false;
    }
    private function anyMob():Boolean {
        try { for each (var c:Object in gi().scene.getCharsByType(2)) if (c.usable && !isDead(c)) return true; } catch (e:Error) { }
        return false;
    }

    // ---- Liên Trảm: đi nhanh theo tuyến, tích chuỗi; từ mốc X thì dụ quái (mỗi điểm giết 1-2 con rồi đi tiếp, lính bám theo);
    //      sắp hết thời gian giữ chuỗi (còn < 40%) thì dừng đánh con gần nhất, ra chiêu dồn; đạt mốc Y thì đánh boss (hoặc bỏ qua boss)
    private function ltStep(run:Object, now:int):void {
        var cfg:Object = run.cfg, cnt:int = lzCount();
        if (cnt > int(run.lzMax)) run.lzMax = cnt;
        _map = curMap(); _x = me().tile_x; _y = me().tile_y; _r = cfg.range > 0 ? cfg.range : 99;
        if (_target != null && !valid(_target)) { if (isDead(_target)) { _kills++; _util.perfKill(); } _target = null; }
        if (!run.boss) {
            if (cfg.lty > 0 && cnt >= cfg.lty && !run.reachedY) {
                run.reachedY = true;
                _emit("train", "pb lt liên trảm đạt " + cnt + " (mốc " + cfg.lty + ")" + (cfg.cont ? ", tích tiếp tới khi đứt chuỗi" : ""));
            }
            if (run.reachedY && (!cfg.cont || cnt == 0)) { run.boss = true; _target = null; _emit("train", "pb lt " + (cfg.skipboss ? "dừng tích liên trảm" : "đi đánh boss")); }
        }
        if (run.boss) {
            if (cfg.skipboss) { pbExit("bỏ qua đánh boss", true); return; }
            // boss: thấy boss thì đi thẳng tới rồi đánh boss; chưa thấy thì đi về khu boss (78,65) -> (78,56). Lính bám theo thì kệ (đánh lan sẽ trúng)
            var boss:Object = null;
            try { for each (var bc:Object in gi().scene.getCharsByType(2)) if (bc.usable && !isDead(bc) && isBossMob(bc)) { boss = bc; break; } } catch (e:Error) { }
            if (support(now)) return;
            if (cfg.pick == 1 && pbPickStep(now, PB_R)) { run.progressAt = now; return; }
            if (boss) {
                run.progressAt = now;
                if (tileDist(me(), boss.tile_x, boss.tile_y) > 6) {
                    _target = null;
                    if (me().getStatus() != "walk" && now - int(run.moveAt) > 500) { walkTo(curMap(), boss.tile_x, boss.tile_y); run.moveAt = now; }
                    return;
                }
                _r = 30;
                if (_target != boss) { _target = boss; _targetSince = now; gi().lockOnChar = boss; }
                fight(now);
                return;
            }
            if (run.bossArea && (_target != null || pick(now) != null)) { run.progressAt = now; fight(now); return; }
            if (tileDist(me(), 78, 56) <= 4) run.bossArea = true;
            if (tileDist(me(), 78, 65) <= 3) run.bossVia = true;
            var bp:Array = !run.bossVia ? [78, 65] : [78, 56];
            var bs:String = goPoint(run, bp[0], bp[1], now, 3, 500);
            if (bs == "stuck") { if (!run.bossVia) run.bossVia = true; else run.bossArea = true; }
            return;
        }
        _pbNoBoss = true;
        var win:int = lzWin();
        var rescue:Boolean = cnt > 0 && win > 0 && _lzLastAt > 0 && now - _lzLastAt > win * 0.6;
        if (rescue && !run.rescuing) run.lzRescues++;
        run.rescuing = rescue; _fast = rescue;
        if (!rescue && support(now)) return;
        if (cfg.pick == 1 && !rescue && pbPickStep(now, PB_R)) { run.progressAt = now; return; }
        var kite:Boolean = cfg.lure && cnt >= cfg.ltx;
        var route:Array = cfg.route || [];
        var mob:Object;
        if (!kite || rescue || route.length == 0) {
            mob = _target != null ? _target : ltPick(now, cfg.range > 0 ? cfg.range : 99, false);
            if (mob) { ltFight(run, mob, now); return; }
            run.atPoint = false;
            ltWalk(run, now);
            return;
        }
        var p:Array = nextGoodPoint(run, route, "ri") || route[run.ri % route.length];
        if (run.atPoint || tileDist(me(), p[0], p[1]) <= 3) {
            if (!run.atPoint) { run.atPoint = true; run.pk0 = _kills; run.pc0 = cnt; run.atAt = now; }
            var got:int = Math.max(_kills - int(run.pk0), cnt - int(run.pc0));
            if (got < cfg.kpp && now - run.atAt < 8000) {
                mob = _target != null && tileDist(me(), _target.tile_x, _target.tile_y) <= 6 ? _target : ltPick(now, 6, true);
                if (mob) { ltFight(run, mob, now); return; }
            }
            run.atPoint = false; _target = null;
            ltAdvance(run);
        }
        _target = null;
        ltWalk(run, now);
    }
    private function ltFight(run:Object, mob:Object, now:int):void {
        if (_target != mob) { _target = mob; _targetSince = now; _myMobs[mob.id] = now; gi().lockOnChar = mob; }
        run.lapFound = true; run.progressAt = now;
        fight(now);
    }
    /** lính (không phải boss) trong bán kính; lowHp: ưu tiên con ít máu (chết nhanh, giữ chuỗi), ngang thì con gần */
    private function ltPick(now:int, maxD:Number, lowHp:Boolean):Object {
        var best:Object = null, bk:Number = Number.MAX_VALUE;
        try {
            for each (var c:Object in gi().scene.getCharsByType(2)) {
                if (!c.usable || isDead(c) || isBossMob(c)) continue;
                if (_black[c.id] != undefined && _black[c.id] > now) continue;
                var d:Number = tileDist(me(), c.tile_x, c.tile_y);
                if (d > maxD) continue;
                var k:Number = d;
                if (lowHp) try { var a:Object = c.data.attributeInfo; if (a.hpMax > 0) k = (a.hpNow / a.hpMax) * 1000 + d; } catch (e1:Error) { }
                if (k < bk) { bk = k; best = c; }
            }
        } catch (e:Error) { }
        return best;
    }
    private function ltAdvance(run:Object):void {
        var route:Array = run.cfg.route || [];
        if (route.length == 0) return;
        run.ri++;
        if (run.ri % route.length == 0) {
            if (!run.lapFound && !run.boss) { run.boss = true; _emit("train", "pb lt hết vòng không thấy lính, đi đánh boss"); }
            run.lapFound = false;
        }
    }
    /** đi tới điểm kế, không đứng chờ; lệnh đi lặp lại mỗi 0,5 giây nếu nhân vật đứng lại */
    private function ltWalk(run:Object, now:int):void {
        var route:Array = run.cfg.route || [];
        if (route.length == 0) return;
        var p:Array = nextGoodPoint(run, route, "ri");
        if (p == null) { run.boss = true; return; }
        if (goPoint(run, p[0], p[1], now, 3, 500) != "walking") ltAdvance(run);
    }

    // ---- ải chuột (Doanh Trại / Mê Cung tầng 15 / phòng thần bí): chỉ đánh chuột, đánh chết con nào nhặt hết đồng con đó rồi mới đánh tiếp
    private static const MOUSE_R:int = 200;
    private function pbMice():Array {
        var out:Array = [];
        try { for each (var c:Object in gi().scene.getCharsByType(2)) if (c.usable && !isDead(c) && mobName(c).indexOf("chuột") >= 0) out.push(c); } catch (e:Error) { }
        return out;
    }
    /** true = đang xử lý ải chuột (tick này không làm gì khác) */
    private function pbMouseStep(run:Object, now:int, route:Array):Boolean {
        var mice:Array = pbMice();
        if (mice.length && !run.mouse) { run.mouse = true; run.mouseLapClean = false; run.mri = 0; _emit("train", "pb " + run.key + " ải chuột: chỉ đánh chuột, đánh chết con nào nhặt hết đồng rồi mới đánh tiếp"); }
        if (!run.mouse) return false;
        _map = curMap(); _x = me().tile_x; _y = me().tile_y; _r = MOUSE_R;
        run.progressAt = now;
        // con vừa chết: chờ gói túi đồ của nó (tối đa 1.5s) rồi nhặt hết mới đánh con khác
        var mt:Object = run.mouseTarget;
        if (mt && (isDead(mt) || !mobRefOk(mt, run.mouseTargetId))) {
            if (!run.mouseDeadAt) { run.mouseDeadAt = now; _kills++; _util.perfKill(); }
            if (_lastDropMob[mt.id] == undefined && now - run.mouseDeadAt < 1500) return true;
            run.mouseTarget = null; run.mouseDeadAt = 0; _target = null;
        }
        if (pbPickStep(now, MOUSE_R)) return true;                       // nhặt đồng trước
        if (run.mouseTarget == null) {
            var best:Object = null, bd:Number = Number.MAX_VALUE;
            for each (var c:Object in mice) { var d:Number = tileDist(me(), c.tile_x, c.tile_y); if (d < bd) { bd = d; best = c; } }
            if (best) { run.mouseTarget = best; run.mouseTargetId = int(best.id); _target = best; _targetSince = now; _myMobs[best.id] = now; gi().lockOnChar = best; }
        }
        if (run.mouseTarget) {
            run.quietAt = -1; run.mouseLapClean = false;
            if (_target != run.mouseTarget) { _target = run.mouseTarget; gi().lockOnChar = _target; }
            if (support(now)) return true;
            fight(now);
            return true;
        }
        // không thấy chuột: đi tuần tìm tiếp; đi hết một vòng không thấy con nào và hết đồng -> xong ải chuột
        if (route && route.length && !run.mouseLapClean) {
            var p:Array = route[int(run.mri) % route.length];
            if (badPoint(run, p) || goPoint(run, p[0], p[1], now, 2, 500) != "walking") {
                run.mri = int(run.mri) + 1;
                if (run.mri % route.length == 0) run.mouseLapClean = true;
            }
            return true;
        }
        if (run.quietAt < 0) run.quietAt = now;
        if (now - run.quietAt < PB_QUIET) return true;
        _emit("train", "pb " + run.key + " hết chuột và đồng");
        run.mouse = false; run.quietAt = -1; run.mouseDone = true;
        _r = PB_R;
        return false;
    }

    private function pbPatrol(run:Object, now:int):void {
        var route:Array = run.cfg.route;
        if (!route || route.length == 0) return;
        if (run.key == "lt" && run.boss) {                              // hết vòng không thấy quái: tới boss
            if (me().getStatus() != "walk" && now - run.moveAt > WALK_RETRY) {
                var bp:Array = tileDist(me(), 78, 65) > 3 && tileDist(me(), 78, 56) > 3 ? [78, 65] : [78, 56];
                walkTo(curMap(), bp[0], bp[1]); run.moveAt = now;
            }
            return;
        }
        // bỏ qua điểm đã biết là không tới được (vẫn tính vòng tuần)
        var guard:int = 0;
        while (badPoint(run, route[run.ri % route.length]) && guard++ < route.length) patrolAdvance(run, route);
        if (guard >= route.length) { if (!run.allBadLogged) { run.allBadLogged = true; _emit("train", "warn pb mọi điểm tuần ở map " + curMap() + " đều không tới được"); } run.lapEmpty = true; return; }
        var p:Array = route[run.ri % route.length];
        run.patrolling = true;
        if (goPoint(run, p[0], p[1], now, 3, 500) != "walking") patrolAdvance(run, route);   // tới điểm (hoặc không tới được) là đi tiếp
    }
    private function patrolAdvance(run:Object, route:Array):void {
        run.ri++;
        run.noTarget++;
        if (run.ri % route.length == 0) {                              // hết một vòng
            if (!run.lapFound) {
                if (run.key == "lt" && !run.boss) { run.boss = true; _emit("train", "pb lt hết vòng không thấy quái, đi đánh boss"); }
                else run.lapEmpty = true;
            }
            run.lapFound = false;
        }
    }

    /** đi sang tầng kế qua cổng: lệnh đi của game (tự tìm cổng); đứng sát cổng 2s mà chưa sang thì gửi 10051 như game làm khi bước lên cổng */
    private function pbGoMap(next:int, now:int):void {
        var run:Object = _pbRun;
        if (!run.goingGate) { run.goingGate = true; _emit("train", "pb " + run.key + " cổng sang tầng kế đã mở, đi qua cổng"); }
        run.progressAt = now;
        var p:Object = pbPortal(next);
        if (p && tileDist(me(), p.tile_x, p.tile_y) <= 2 && me().getStatus() != "walk") {
            if (!run.atGate) run.atGate = now;
            if (now - run.atGate >= 2000 && _pbAsk == null && now - int(run.s51At) >= 3000) {
                try { proxy("Engine_MsgSenderProxy").send_10051([p.tile_x, p.tile_y]); _emit("train", "pb gửi 10051 tại cổng (" + p.tile_x + "," + p.tile_y + ")"); } catch (e:Error) { }
                run.s51At = now;
            }
            return;
        }
        run.atGate = 0;
        if (_pbAsk != null) return;                                     // đang có câu hỏi qua ải: chờ xác nhận
        // gửi lại chỉ khi lệnh trước đã 2s mà nhân vật không nhích; đang đi / đã nhích thì chờ tới 8s (gửi dồn sẽ hủy lệnh tìm đường)
        var gmMoved:Boolean = me().getStatus() == "walk" || me().tile_x != int(run.gmX) || me().tile_y != int(run.gmY);
        if (run.moveAt > 0 && gmMoved && now - run.moveAt < 8000) { run.gmX = me().tile_x; run.gmY = me().tile_y; return; }
        if (now - run.moveAt < 2000) return;
        if (!walkCmd(next + ",-1,-1,0", null, "sang tầng", -1, -1)) return;   // game tự tìm cổng sang map đó
        run.moveAt = now; run.gmX = me().tile_x; run.gmY = me().tile_y;
    }

    private function pbPortal(map:int):Object {
        try { for each (var c:Object in gi().scene.getCharsByType(7)) if (c.data && c.data.res && int(c.data.res.mapID) == map) return c; } catch (e:Error) { }
        return null;
    }
    private function pbPortalTo(map:int):Boolean { return pbPortal(map) != null; }
    /** cổng sang map kế đã mở: 10300 (map = map đang đứng, isOpen = 1) hoặc cờ opened của cổng trong game (game bật khi nhận 10300) */
    private function pbPortalOpen(map:int):Boolean {
        if (_pbGateAt > 0) return true;
        try { var p:Object = pbPortal(map); return p != null && p.data.res.opened == true; } catch (e:Error) { }
        return false;
    }

    /** còn túi đồ cần nhặt quanh đây (theo luật nhặt, bỏ qua túi đang tạm bỏ) */
    private function pbBagsNear():Boolean {
        if (_pickMode == "off") return false;
        try { return chooseBag(getTimer()) != null; } catch (e:Error) { }
        return false;
    }

    // ---- hoàn thành: nhận thưởng (như nút nhận ở bảng thông quan), rồi ra
    private function pbRewardStep(now:int):void {
        if (_pbAt > 0 && now - _pbAt >= 1500 && !_pbRun.rewardSent) {
            try { _pbTeam.send_10723(); } catch (e:Error) { }
            _pbRun.rewardSent = true;
            _emit("train", "pb reward " + _pbRun.key);
        }
        if (_pbRun.rewardSent && now - _pbAt >= 5500) {
            if (now - _pbAt < 15500 && pbPickStep(now, 30)) return;    // nhặt nốt đồ quanh đó (tối đa 10 giây)
            closeInstancePanel();
            pbExit("");
        }
    }

    /** 90s không tiến triển: không thoát phó bản — xóa điểm hỏng / quái bị bỏ qua, dừng lệnh đi, đi tuần lại từ đầu */
    private function pbUnstick(run:Object, now:int, where:String):void {
        run.unstuck = int(run.unstuck) + 1;
        _emit("train", "warn pb " + run.key + " " + where + ": " + int(PB_FLOOR_STUCK / 1000) + " giây không tiến triển — gỡ kẹt (lần " + run.unstuck
              + "): xóa điểm hỏng + quái bị bỏ qua, đi tuần lại từ đầu");
        run.badPts = {}; run.ri = 0; run.gp = null; run.lapFound = false; run.lapEmpty = false; run.allBadLogged = false; run.patrolling = false;
        run.mcTried = null; run.mcDoorAt = 0; run.mcNear = false;
        _black = {}; _blackN = {}; _target = null; _ap = null; _pp = null; _mv = null;
        try { _c.MainCharSeachPathManager.clear(); me().stopMove(); } catch (e:Error) { }
        run.progressAt = now; run.floorAt = now;
    }

    private function pbExit(why:String, ok:Boolean = false):void {
        if (why) _emit("train", "pb exit " + _pbRun.key + " " + why);
        _pbRun.failWhy = ok ? "" : why;
        _pbRun.okWhy = ok ? why : "";
        _pbPhase = "exit"; _pbAt = getTimer(); _pbRun.moveAt = 0; _target = null;
    }

    private function pbExitStep(now:int):void {
        var here:int = curMap();
        if (pbKeyOfMap(here) != _pbRun.key) {
            if (here == PB_TOWN || !pbKeyOfMap(here)) pbEndRun(!_pbRun.failWhy, _pbRun.failWhy || _pbRun.okWhy || "hoàn thành");
            return;
        }
        if (now - _pbAt > 120000) { _emit("train", "warn pb 2 phút chưa ra được khỏi phó bản"); _pbAt = now; }
        if (me().getStatus() != "walk" && now - _pbRun.moveAt > 3000) {
            if (!walkCmd(PB_TOWN + ",-1,-1,0", null, "ra cổng", -1, -1)) return;   // đi vào cổng ra
            _pbRun.moveAt = now;
            _pbRun.exitWalkAt = now;
        }
    }

    /** hồi sinh qua bảng hồi sinh của game: tại chỗ (tốn hoa) hoặc về thành */
    private function pbRevive(local:Boolean):void {
        var done:Boolean = false;
        try {
            var PW:Object = _c.POPWindowManager, w:Object = PW ? PW.popWindow : null;
            if (w && w.parent && w.numChildren > 0 && getQualifiedClassName(w.getChildAt(0)).indexOf("ReLivePanel") >= 0 && _c.BaseEvent) {
                var BE:Class = _c.BaseEvent as Class;
                w.getChildAt(0).dispatchEvent(new BE(local ? "UI_RELIVEPANEL_LOCALE_RELIVE_BTN_CLICK" : "UI_RELIVEPANEL_RETURN_RELIVE_BTN_CLICK"));
                PW.closeWindow();
                done = true;
            }
        } catch (e:Error) { }
        if (!done) {                                                     // không thấy bảng: gửi như nút của bảng (20075)
            var ba:ByteArray = new ByteArray();
            ba.writeByte(local ? 0 : 1);
            ba.writeDouble(getTimer());
            _c.NetWorkManager.sendMsg(20075, ba);
        }
        _emit("train", "pb revive " + (local ? "tại chỗ bằng hoa hồng" : "về thành") + " (chết " + (_pbRun ? _pbRun.deaths : 0) + " lần trong lượt)");
    }

    /** bấm "Xác nhận" trên hộp hỏi của game: qua ải (10052) / rời phó bản (lúc đang đi ra) */
    private function pbConfirmDialogs(now:int):void {
        if (!_pbRun) return;
        if (_pbAsk != null && (curMap() != _pbAsk.map || now - _pbAsk.at > 15000)) _pbAsk = null;   // đã sang tầng / quá hạn
        // dự phòng: 3 giây sau 10052 vẫn chưa sang tầng (hộp không hiện / bấm không ăn) -> gửi 10053 [x,y] của 10052, đúng như nút Xác nhận của game
        if (_pbAsk != null && !_pbAsk.s53 && now - _pbAsk.at >= 3000) {
            _pbAsk.s53 = true;
            if (_pbRun.key == "tq" && roses() < 1) { _emit("train", "warn pb hết hoa hồng, không qua ải"); pbExit("hết hoa hồng"); _pbAsk = null; return; }
            try { proxy("Engine_MsgSenderProxy").send_10053([_pbAsk.x, _pbAsk.y]); _emit("train", "pb confirm qua ải (gửi 10053 dự phòng)"); } catch (e0:Error) { }
            return;
        }
        var wantAsk:Boolean = _pbAsk != null && !_pbAsk.clicked;
        var wantExit:Boolean = _pbPhase == "exit" && _pbRun.exitWalkAt > 0 && now - _pbRun.exitWalkAt < 20000;
        if (!wantAsk && !wantExit) return;
        var st:Object = gi().stage;
        if (!st) return;
        for (var i:int = st.numChildren - 1; i >= 0; i--) {
            var ch:Object = st.getChildAt(i);
            if (getQualifiedClassName(ch).indexOf("FAlert") < 0) continue;
            var txt:String = "";
            try { txt = String(ch.text.text); } catch (e:Error) { }
            var ok:Object = findByName(ch, "FAlert.ok");
            if (!ok) continue;
            if (wantAsk && _pbRun.key == "tq" && roses() < 1) { _emit("train", "warn pb hết hoa hồng, không qua ải"); pbExit("hết hoa hồng"); _pbAsk = null; return; }
            ok.dispatchEvent(new MouseEvent(MouseEvent.CLICK));
            _emit("train", "pb confirm " + (wantAsk ? "qua ải: " + (_pbAsk.msg || txt) : "rời phó bản"));
            if (wantAsk) _pbAsk.clicked = true; else _pbRun.exitWalkAt = 0;
            return;
        }
    }

    private static function findByName(o:Object, name:String):Object {
        try {
            if (o.name == name) return o;
            if (o.hasOwnProperty("numChildren")) for (var i:int = 0; i < o.numChildren; i++) { var r:Object = findByName(o.getChildAt(i), name); if (r) return r; }
        } catch (e:Error) { }
        return null;
    }

    private function closeInstancePanel():void {
        try {
            var p:Object = gi().uiInstance._instanceSelectPanel;
            if (p && p.parent && _c.FCloseEvent) { var FE:Class = _c.FCloseEvent as Class; p.dispatchEvent(new FE("close", null)); }
        } catch (e:Error) { }
    }

    /** tầm nhảy của game: OtherConst.JUMP_MAX_DIS (> 0), không đọc được thì 8 ô như bot gốc */
    private function gameJumpMax():int {
        try { if (_c.OtherConst && int(_c.OtherConst.JUMP_MAX_DIS) > 0) return int(_c.OtherConst.JUMP_MAX_DIS); } catch (e:Error) { }
        return 8;
    }

    /**
     * Nhảy quanh quái (Doanh Trại: mọi ải; Phu Tử: boss trạng thái đặc biệt) — như Shift+click của người chơi: MainCharSeachPathManager.charJump.
     * Điều kiện của game: map cho nhảy, thể lực >= 20, không bị trói; thêm: quái trong 7 ô, cách lần trước >= 0.5s.
     */
    /**
     * maxd: tham số tầm nhảy truyền cho charJump. <= 0 = theo game (OtherConst.JUMP_MAX_DIS, không đọc được thì 8 ô) và
     * bỏ điểm đáp ngoài tầm như bot gốc (Doanh Trại); > 0 = truyền nguyên, không lọc (Phu Tử giữ 500 như cũ).
     */
    private function pbJump(mob:Object, now:int, maxd:int):Boolean {
        var here:int = curMap();
        if (now - _pbJumpAt < PB_JUMP_GAP) return false;
        var m:Object = me();
        var st:String = m.getStatus();
        if (st == "walk" || st == "jump") return false;
        try {
            if (m.isJumping() || m.on2Jumping() || m.on3Jumping()) return false;
            if (m.data.isSoft) return false;
            if (Number(gi().mainCharData.attributeInfo.ppNow) < 20) return false;
            if (_c.MapTransManager && !_c.MapTransManager.getMapRes(here).allowJump) {   // ải game không cho nhảy: bỏ qua, không ép
                if (!_noJumpLog[here]) { _noJumpLog[here] = true; _emit("train", "info pb map " + here + " game không cho nhảy — ải này không nhảy"); }
                return false;
            }
            if (_c.FightManager && !_c.FightManager.isMainCharCanMove()) return false;
        } catch (e:Error) { return false; }
        if (tileDist(m, mob.tile_x, mob.tile_y) > 7) return false;
        var game:Boolean = maxd <= 0;
        var jmax:int = game ? gameJumpMax() : maxd;
        for (var t:int = 0; t < 12; t++) {
            _pbJumpDir = (_pbJumpDir + 1 + int(Math.random() * 3)) % 12;
            var nx:int = mob.tile_x + PB_JUMP_OFS[_pbJumpDir][0], ny:int = mob.tile_y + PB_JUMP_OFS[_pbJumpDir][1];
            if (nx == m.tile_x && ny == m.tile_y) continue;
            var jd:Number = tileDist(m, nx, ny);
            if (game && jd > jmax) continue;                            // ngoài tầm nhảy của game: thử hướng khác
            try {
                _c.MainCharSeachPathManager.clear();
                _c.MainCharSeachPathManager.charJump(m, new Point(nx, ny), -1, jmax, null, false, false);
            } catch (e2:Error) { return false; }
            _pbJumpAt = now;
            return true;
        }
        return false;
    }

    // ---------------------------------------------------------------- Phu Tử Trận (map 20175)
    // Đánh quái theo thứ tự NPC 1738 gợi ý (dòng chữ trên đầu NPC: GameInstance.npcTishiDict[1738]).
    // Trước boss: tắt ám khí, tắt cung, cất thú chiến (để không đánh trúng con sai thứ tự); không dùng buff / hồi máu.
    // Boss Khôi Khôi: thả lại thú chiến (vẫn cưỡi ngựa), dùng hỗ trợ, tùy chọn nhảy khi boss mang trạng thái đặc biệt (buff res.type 26).
    private static const PT_NPC:int = 1738;
    private static const PT_NPC_X:int = 12, PT_NPC_Y:int = 14;
    private static const PT_AREA_X:int = 36, PT_AREA_Y:int = 25;
    private var _ptRestore:Object = null;     // {anqi, pet, bow} cần trả lại khi ra khỏi Phu Tử Trận

    private function proxy(name:String):Object {
        if (!_proxies) _proxies = {};
        if (!_proxies[name] && _c[name]) { try { var C:Class = _c[name] as Class; _proxies[name] = new C(); } catch (e:Error) { } }
        return _proxies[name];
    }
    private var _proxies:Object;

    private static function cleanName(s:String):String {
        s = s.replace(/<[^>]*>/g, " ").replace(/\[[^\]]*\]/g, " ").replace(/【[^】]*】/g, " ");
        return s.replace(/\s+/g, " ").toLowerCase().replace(/^\s+|\s+$/g, "");
    }
    private static function mobName(c:Object):String {
        var n:String = "";
        try { n = String(c.getHeadFaceNickName()); } catch (e:Error) { }
        if (!n || n == "null") try { n = String(c.headFace.nickName); } catch (e2:Error) { }
        if (!n || n == "null") try { n = String(c.data.name); } catch (e3:Error) { }
        if (!n || n == "null") try { n = String(c.data.res.name); } catch (e4:Error) { }
        return cleanName(n == "null" ? "" : n);
    }
    private function ptOrder():Array {
        var t:String = "";
        try { var d:Object = gi().npcTishiDict; if (d && d[PT_NPC] != undefined) t = String(d[PT_NPC]); } catch (e:Error) { }
        if (!t) return [];
        t = t.replace(/<[^>]*>/g, " ").replace(/\[[^\]]*\]/g, " ").replace(/【[^】]*】/g, " ").toLowerCase();
        t = t.replace(/gợi ý lượt này/g, " ").replace(/[,，:：;；|\/\\\t\r\n]/g, "|");
        // tên quái có thể nhiều chữ: giữ nguyên cụm nếu trong map có quái đúng tên đó, không thì tách theo khoảng trắng
        var names:Object = {};
        try { for each (var c:Object in gi().scene.getCharsByType(2)) names[mobName(c)] = true; } catch (e2:Error) { }
        var out:Array = [];
        for each (var chunk:String in t.split("|")) {
            chunk = chunk.replace(/\s+/g, " ").replace(/^\s+|\s+$/g, "");
            if (!chunk) continue;
            if (names[chunk] || chunk.indexOf(" ") < 0) { out.push(chunk); continue; }
            for each (var w:String in chunk.split(" ")) if (w.length > 0) out.push(w);
        }
        return out;
    }

    private function ptPrep(run:Object, now:int):void {
        if (run.ptPrepped) return;
        run.ptPrepped = true;
        var anqi:Boolean = false, pet:int = -1;
        try { anqi = int(gi().mainCharData.anqiInfo.isOpen) == 1; } catch (e:Error) {
            try { anqi = int(me().data.anqiInfo.isOpen) == 1; } catch (e1:Error) { } }
        try { pet = int(gi().mainCharData.mountInfo.currentFightMountID); } catch (e2:Error) { }
        if (!_ptRestore) _ptRestore = { anqi: anqi, pet: pet, bow: run.cfg.bow };
        try { if (anqi) { proxy("Fight_MsgSenderProxy").send_52005(0); _emit("train", "pb pt tắt ám khí"); } } catch (e3:Error) { }
        try { proxy("BowArrow_MsgSenderProxy").send_53043(0); _emit("train", "pb pt tắt cung"); } catch (e4:Error) { }
        try { if (pet > 0) { proxy("Mount_MsgSenderProxy").send_50039(pet); _emit("train", "pb pt cất thú chiến #" + pet); } } catch (e5:Error) { }
    }

    /** ra khỏi Phu Tử Trận: trả lại ám khí / cung / thú chiến như lúc đầu */
    private function ptRestore():void {
        if (!_ptRestore) return;
        var r:Object = _ptRestore; _ptRestore = null;
        try { if (r.anqi) proxy("Fight_MsgSenderProxy").send_52005(1); } catch (e:Error) { }
        try { if (r.bow) proxy("BowArrow_MsgSenderProxy").send_53043(1); } catch (e2:Error) { }
        try { var cur:int = int(gi().mainCharData.mountInfo.currentFightMountID); if (r.pet > 0 && cur != r.pet) proxy("Mount_MsgSenderProxy").send_50037(r.pet); } catch (e3:Error) { }
        _emit("train", "pb pt đã trả lại ám khí / cung / thú chiến như cũ");
    }

    private function ptBoss():Object {
        var best:Object = null, bd:Number = Number.MAX_VALUE;
        try {
            for each (var c:Object in gi().scene.getCharsByType(2)) {
                if (!c.usable || isDead(c)) continue;
                if (mobName(c).indexOf("khôi") < 0) continue;
                var d:Number = tileDist(me(), c.tile_x, c.tile_y);
                if (d < bd) { bd = d; best = c; }
            }
        } catch (e:Error) { }
        return best;
    }

    private function ptStep(run:Object, now:int):void {
        ptPrep(run, now);
        _map = curMap(); _x = me().tile_x; _y = me().tile_y; _r = Math.max(40, run.cfg.range > 0 ? run.cfg.range : 99);
        var boss:Object = ptBoss();
        if (boss) {
            if (!run.ptBoss) {
                run.ptBoss = true;
                _emit("train", "pb pt gặp boss Khôi Khôi");
                try { if (_ptRestore && _ptRestore.pet > 0) proxy("Mount_MsgSenderProxy").send_50037(_ptRestore.pet); } catch (e:Error) { }
            }
            run.progressAt = now;
            if (support(now)) return;
            if (_target != boss) { _target = boss; _targetSince = now; gi().lockOnChar = boss; }
            if (run.cfg.jump && ptBossSpecial(boss) && pbJump(boss, now, PT_JUMP_MAX)) return;
            fight(now);
            return;
        }
        if (pbPickStep(now, PB_R)) { run.progressAt = now; return; }
        // thứ tự đợt này
        if (!run.ptOrder || run.ptIdx >= run.ptOrder.length) {
            if (run.ptOrder && run.ptIdx >= run.ptOrder.length && run.ptOrder.length > 0) { _emit("train", "pb pt đánh hết đợt, quay lại hỏi NPC"); run.ptOrder = null; }
            if (tileDist(me(), PT_NPC_X, PT_NPC_Y) > 3) {
                if (me().getStatus() != "walk" && now - run.moveAt > 3000) { walkTo(curMap(), PT_NPC_X, PT_NPC_Y + 1); run.moveAt = now; }
                return;
            }
            if (now - int(run.ptAskAt) >= 2000) {
                run.ptAskAt = now;
                try {
                    var npc:Object = gi().scene.getCharByID(PT_NPC, 6);
                    if (npc) gi().lockOnChar = npc;
                    proxy("MainChar_MsgSenderProxy").send_10129(PT_NPC);
                } catch (e2:Error) { }
            }
            var ord:Array = ptOrder();
            if (ord.length > 0 && ord.join(" ") != run.ptLast) {
                run.ptOrder = ord; run.ptIdx = 0; run.ptLast = ord.join(" "); run.progressAt = now;
                _emit("train", "pb pt thứ tự: " + ord.join(", "));
                closeNpcDialog();
            }
            return;
        }
        var want:String = run.ptOrder[run.ptIdx];
        var pt:Object = run.ptTarget;                                       // con đang đánh theo thứ tự (fight() có thể tự bỏ _target khi nó chết)
        if (pt != null && (isDead(pt) || !mobRefOk(pt, run.ptTargetId))) {
            if (run.ptTargetName == want) { run.ptIdx++; run.progressAt = now; _emit("train", "pb pt đã hạ " + want); }
            run.ptTarget = null; _target = null;
            return;
        }
        if (pt != null && _target != pt) { _target = pt; gi().lockOnChar = pt; }
        if (_target == null) {
            var best:Object = null, bd:Number = Number.MAX_VALUE;
            for each (var c:Object in gi().scene.getCharsByType(2)) {
                if (!c.usable || isDead(c)) continue;
                var nm:String = mobName(c);
                if (nm != want && nm.indexOf(want) < 0) continue;
                var d:Number = tileDist(me(), c.tile_x, c.tile_y);
                if (d > (run.cfg.range > 0 ? run.cfg.range : 99)) continue;
                if (d < bd) { bd = d; best = c; }
            }
            if (!best) {                                                   // chưa thấy: xuống khu quái rồi đi tuần
                var route:Array = run.cfg.route;
                var p:Array = run.ptArea ? route[run.ri % route.length] : [PT_AREA_X, PT_AREA_Y];
                var pg:String = run.ptArea && badPoint(run, p) ? "stuck" : goPoint(run, p[0], p[1], now, 2, 500);
                if (pg != "walking") { if (!run.ptArea) { run.ptArea = true; _emit("train", "pb pt đã tới khu quái"); } else run.ri++; }
                return;
            }
            _target = best; _targetSince = now; run.ptTargetName = want; run.ptTarget = best; run.ptTargetId = int(best.id); gi().lockOnChar = best;
        }
        run.progressAt = now;
        fight(now);                                                        // không gọi support(): chưa tới boss thì không buff / hồi máu
    }

    private static function ptBossSpecial(boss:Object):Boolean {
        try { for each (var b:Object in boss.data.buffInfo.buffArr) if (b && b.res && int(b.res.type) == 26) return true; } catch (e:Error) { }
        return false;
    }

    private function closeNpcDialog():void {
        try { pipe("HIDE_NPC_DIALOG_PLANE"); } catch (e:Error) { }                // như phím Esc với hộp thoại NPC
    }

    // ---------------------------------------------------------------- Mê Cung Trận
    // 16 phòng 20033, 20177..20191, mỗi phòng 2 cửa: trái (40,26) / phải (116,28). Đi vào cửa (game tự dịch chuyển như bước lên cổng).
    // Sang đúng phòng kế: nhớ cửa đúng. Bị đưa đi chỗ khác / 4s vẫn ở phòng cũ: đổi cửa. Phòng cuối: NPC 1740 (84,64) nhận quà (52059) rồi ra.
    // Phòng thần bí 20192: đánh chuột, nhặt đồng rồi ra. Tùy chọn: dừng ở một tầng để treo đánh quái N phút.
    // Leo tầng: không đánh, không nhặt — đi thẳng tới cửa (quái ra liên tục). Tầng treo: đứng ở (76,51), quái tự tới thì đánh,
    // nhặt theo cài đặt nhặt của Đánh quái; ngưng treo khi hết N phút HOẶC (chọn 1 trong 2) đạt mốc liên trảm X.
    // Tùy chọn bỏ qua ải chuột tầng 15 + phòng thần bí: không đánh, đi thẳng tới cửa / cổng (game cho qua khi chưa giết hết).
    private static const MC_FARM_X:int = 76, MC_FARM_Y:int = 51;
    private static const MC_FARM_R:int = 6;              // đánh quái trong 6 ô quanh điểm treo
    private static const MC_ROOMS:Array = [20033,20177,20178,20179,20180,20181,20182,20183,20184,20185,20186,20187,20188,20189,20190,20191];
    private static const MC_SECRET:int = 20192;
    private static const MC_DOORS:Object = { L: [40, 26], R: [116, 28] };
    private static const MC_NPC:int = 1740;
    // phòng thần bí: MeCungThanBi.xml (75,51) + MeCung15.xml (10 điểm) của bạn
    private static const MC_SECRET_ROUTE:Array = [[75,51]];                                   // MeCungThanBi.xml
    private static const MC_R15_ROUTE:Array = [[113,67],[92,55],[71,49],[18,51],[59,72],[103,45],[121,40],[63,40],[79,21],[45,30]];   // MeCung15.xml: tầng 15

    private function mcStep(run:Object, now:int):void {
        var here:int = curMap();
        var room:int = MC_ROOMS.indexOf(here);
        if (!run.mcDoor) { run.mcDoor = []; run.mcFrom = -1; run.mcCur = -1; }
        _map = here; _x = me().tile_x; _y = me().tile_y; _r = PB_R;
        // vừa đổi phòng: đánh giá cửa vừa đi
        if (here != run.mcCur) {
            var from:int = run.mcFrom;
            if (from >= 0 && run.mcTried) {
                var ok:Boolean = room == from + 1;
                if (here == MC_SECRET) {                                        // cửa này dẫn vào phòng thần bí: lần sau thử cửa kia
                    run.mcDoor[from] = run.mcTried == "L" ? "R" : "L";
                } else if (!ok) {
                    run.mcDoor[from] = run.mcTried == "L" ? "R" : "L";
                    _emit("train", "pb mc phòng " + (from + 1) + ": cửa " + (run.mcTried == "L" ? "trái" : "phải") + " sai, lần sau đi cửa kia");
                } else if (ok) run.mcDoor[from] = run.mcTried;
            }
            run.mcCur = here; run.mcTried = null; run.mcNear = false; run.mouse = false; run.mouseDone = false; run.mouseTarget = null; run.mcArriveAt = now; run.moveAt = 0; run.quietAt = -1; run.mcSi = 0; run.mcSecretLap = false;
            if (here == MC_SECRET) _emit("train", "pb mc vào phòng thần bí: đánh chuột, nhặt đồng");
            else _emit("train", "pb floor mc " + (room + 1) + "/" + MC_ROOMS.length + " map " + here);
        }
        if (room >= 0) run.floor = room;
        if (here == MC_SECRET) {
            if (run.cfg.skipMice) { mcSecretExit(run, now); return; }           // bỏ qua phòng thần bí: ra cổng luôn
            if (!pbMouseStep(run, now, run.cfg.sroute && run.cfg.sroute.length ? run.cfg.sroute : MC_SECRET_ROUTE)) mcSecret(run, now);
            return;
        }
        if (room == 14 && !run.cfg.skipMice && !run.mouseDone && pbMouseStep(run, now, run.cfg.r15 && run.cfg.r15.length ? run.cfg.r15 : MC_R15_ROUTE)) return;   // tầng 15: ải chuột
        if (room < 0) return;
        // treo đánh quái ở tầng chỉ định
        if (run.cfg.farm > 0 && room + 1 == run.cfg.farm && !run.mcFarmDone && mcFarmStep(run, now)) return;
        // leo tầng: không đánh, không nhặt — bỏ mục tiêu / túi đang theo, đi thẳng tới cửa
        if (_target != null || _pickBag != null || _pp != null) {
            _target = null; _pickBag = null; _pp = null; _ap = null;
            try { gi().lockOnChar = null; } catch (e0:Error) { }
        }
        // phòng cuối: nhận quà NPC rồi ra
        if (room == MC_ROOMS.length - 1) {
            if (tileDist(me(), 84, 64) > 3) {
                if (me().getStatus() != "walk" && now - run.moveAt > 1000) { walkTo(here, 84, 64); run.moveAt = now; }
                return;
            }
            if (!run.mcGift) {
                run.mcGift = now;
                try {
                    var npc:Object = gi().scene.getCharByID(MC_NPC, 6);
                    if (npc) gi().lockOnChar = npc;
                    _pbTeam.send_52059();
                } catch (e:Error) { }
                _emit("train", "pb mc nhận quà NPC phòng cuối");
                return;
            }
            if (now - run.mcGift > 3000) { closeNpcDialog(); pbExit(""); }           // xong mê cung: ra
            return;
        }
        // chọn cửa
        var door:String = run.mcDoor[room] || "L";
        var dp:Array = MC_DOORS[door];
        var portal:Object = null;
        try { for each (var c:Object in gi().scene.getCharsByType(7)) if (tileDist(c, dp[0], dp[1]) <= 5) { portal = c; break; } } catch (e2:Error) { }
        var tx:int = portal ? portal.tile_x : dp[0], ty:int = portal ? portal.tile_y : dp[1];
        // đã tới sát cửa rồi bị đưa về chỗ xa trong CÙNG phòng (sai cửa ở phòng đầu): đổi cửa
        if (run.mcTried == door && run.mcNear && tileDist(me(), tx, ty) > 10) {
            run.mcDoor[room] = door == "L" ? "R" : "L";
            _emit("train", "pb mc phòng " + (room + 1) + ": cửa " + (door == "L" ? "trái" : "phải") + " sai (bị đưa lại), lần sau đi cửa kia");
            run.mcTried = null; run.mcNear = false;
            return;
        }
        if (!run.mcTried || run.mcTried != door) { run.mcTried = door; run.mcFrom = room; run.mcDoorAt = 0; run.mcNear = false; }
        if (tileDist(me(), tx, ty) <= 3) run.mcNear = true;
        if (tileDist(me(), tx, ty) <= 0.5) {
            if (!run.mcDoorAt) run.mcDoorAt = now;
            if (now - run.mcDoorAt > 4000) {                                   // 4s vẫn ở phòng cũ: đổi cửa
                run.mcDoor[room] = door == "L" ? "R" : "L";
                _emit("train", "pb mc phòng " + (room + 1) + ": cửa " + (door == "L" ? "trái" : "phải") + " không đi được, thử cửa kia");
                run.mcTried = null;
            }
            return;
        }
        run.progressAt = now;
        if (me().getStatus() != "walk" && now - run.moveAt > 1000) { walkTo(here, tx, ty); run.moveAt = now; }
    }

    /** tầng treo; true = còn treo (nhịp này đã xử lý), false = ngưng treo, đi tiếp */
    private function mcFarmStep(run:Object, now:int):Boolean {
        var lz:Boolean = run.cfg.farmBy == "lz";
        if (!run.mcFarm) {
            run.mcFarm = now; run.mcLzMax = 0;
            _target = null; _pickBag = null;
            _emit("train", "pb mc dừng ở tầng " + run.cfg.farm + " treo quái tại " + MC_FARM_X + "," + MC_FARM_Y + ", ngưng khi "
                  + (lz ? "đạt liên trảm " + run.cfg.farmLz : "hết " + run.cfg.farmMin + " phút"));
        }
        run.progressAt = now;
        var c:int = lzCount();
        if (c > run.mcLzMax) run.mcLzMax = c;
        var done:String = null;
        if (lz) {
            if (c >= run.cfg.farmLz) done = "đạt liên trảm " + c;
        } else if (now - run.mcFarm >= run.cfg.farmMin * 60000) done = "hết " + run.cfg.farmMin + " phút";
        if (done) {
            run.mcFarmDone = true;
            _target = null; _pickBag = null; _ap = null;
            try { gi().lockOnChar = null; } catch (e:Error) { }
            _emit("train", "pb mc ngưng treo: " + done + " — đi tiếp");
            return false;
        }
        _map = curMap(); _x = MC_FARM_X; _y = MC_FARM_Y; _r = MC_FARM_R;        // vùng treo: quanh điểm treo
        if (support(now)) return true;
        if (pickStep(now)) return true;                                       // nhặt theo cài đặt nhặt của Đánh quái
        if (_target != null || _dw != null || pick(now) != null) { fight(now); return true; }   // quái tự tới: đánh (fight tự tính kill, chờ đồ rơi)
        if (tileDist(me(), MC_FARM_X, MC_FARM_Y) > 2 && me().getStatus() != "walk" && now - run.moveAt > 1000) {
            walkTo(curMap(), MC_FARM_X, MC_FARM_Y, "điểm treo mê cung"); run.moveAt = now;
        }
        return true;
    }

    /** ra khỏi phòng thần bí bằng cổng trong phòng (không cổng thì đi về phòng vừa rời) */
    private function mcSecretExit(run:Object, now:int):void {
        run.progressAt = now;
        if (_target != null || _pickBag != null) { _target = null; _pickBag = null; try { gi().lockOnChar = null; } catch (e0:Error) { } }
        var portal:Object = null;
        try { for each (var c:Object in gi().scene.getCharsByType(7)) { portal = c; break; } } catch (e:Error) { }
        if (me().getStatus() != "walk" && now - run.moveAt > 1000) {
            if (portal) walkTo(curMap(), portal.tile_x, portal.tile_y, "ra phòng thần bí");
            else if (run.mcFrom >= 0) walkCmd(MC_ROOMS[run.mcFrom] + ",-1,-1,0", null, "ra phòng thần bí", -1, -1);
            run.moveAt = now;
        }
    }

    private function mcSecret(run:Object, now:int):void {
        _r = 40;
        run.progressAt = now;
        if (support(now)) return;
        if (pbPickStep(now, 40)) return;
        var mob:Object = _target != null && valid(_target) ? _target : pick(now);
        if (mob) { run.quietAt = -1; fight(now); return; }
        _target = null;
        var sr:Array = run.cfg.sroute && run.cfg.sroute.length ? run.cfg.sroute : MC_SECRET_ROUTE;
        if (!run.mcSecretLap) {                                                // đi một vòng các điểm tìm chuột
            var si:int = int(run.mcSi);
            if (si < sr.length) {
                var sp:Array = sr[si];
                if (tileDist(me(), sp[0], sp[1]) > 2) { if (me().getStatus() != "walk" && now - run.moveAt > 1000) { walkTo(curMap(), sp[0], sp[1]); run.moveAt = now; } return; }
                run.mcSi = si + 1;
                return;
            }
            run.mcSecretLap = true;
        }
        if (run.quietAt < 0) run.quietAt = now;
        if (now - run.quietAt < PB_QUIET) return;
        mcSecretExit(run, now);                                               // hết chuột: ra bằng cổng trong phòng
    }

    // ------------------------------------------------------------------ gọi vào game

    private function walkTo(map:int, x:int, y:int, src:String = "khác"):void { moveTo(map, x, y, 0, src); }

    // ---- MỌI lệnh đi của tool đi qua walkCmd: tối đa 1 lệnh / 0,7s (mọi nguồn), ghi nguồn để chẩn đoán "spam đi"
    private var _wHist:Array = [];             // [{at, src, x, y, fx, fy}] trong 5s gần nhất
    private var _lastWalkCmd:int = -10000;
    private var _spamLogAt:int = -60000;
    private function walkCmd(spec:String, cb:Object, src:String, x:int, y:int, five:Boolean = true):Boolean {
        var now:int = getTimer(), m:Object = me();
        if (now - _lastWalkCmd < 700) return false;
        try {
            if (five) _c.MainCharSeachPathManager.mainCharWalk(spec, false, cb, true, true, false);
            else _c.MainCharSeachPathManager.mainCharWalk(spec, false, cb, true, false);
        } catch (e:Error) { return false; }
        _lastWalkCmd = now;
        _wHist.push({ at: now, src: src, x: x, y: y, fx: m.tile_x, fy: m.tile_y });
        while (_wHist.length && now - _wHist[0].at > 5000) _wHist.shift();
        spamCheck(now);
        return true;
    }
    /** 5 giây có >= 4 lệnh đi mà nhân vật nhích chưa tới 2 ô -> ghi 1 dòng (tối đa 1 dòng / 10s) */
    private function spamCheck(now:int):void {
        if (_wHist.length < 4 || now - _spamLogAt < 10000) return;
        var f:Object = _wHist[0], m:Object = me();
        if (Math.abs(m.tile_x - f.fx) + Math.abs(m.tile_y - f.fy) >= 2) return;
        _spamLogAt = now;
        var parts:Array = [];
        for each (var h:Object in _wHist) parts.push(h.src + (h.x >= 0 ? "(" + h.x + "," + h.y + ")" : ""));
        var t:Object = _target;
        _emit("train", "info spam đi: " + _wHist.length + " lệnh trong 5 giây, nhân vật nhích chưa tới 2 ô; lệnh: " + parts.join(" ")
              + "; trạng thái=" + m.getStatus() + ", khống chế=" + (charFixed() ? "có" : "không")
              + ", mục tiêu=" + (t ? "#" + t.id + " cách " + tileDist(m, t.tile_x, t.tile_y).toFixed(1) + " ô" : "-")
              + ", nhặt=" + (_pp ? "#" + _pp.bag.id : _pickBag ? "#" + _pickBag.id : "-")
              + ", hỗ trợ gần nhất " + (now - _lastSupport) + "ms, chiêu gần nhất " + (now - _lastSkill) + "ms");
    }

    /**
     * Mọi lệnh đi của tool đi qua đây. Không gửi lại cùng một điểm đến dồn dập: game cần thời gian tìm đường + bắt đầu bước,
     * gửi lại liên tục sẽ hủy lệnh trước (nhân vật đứng yên dù điểm đến ở xa, không vướng gì).
     *  - cùng điểm đến (lệch <= 1 ô) mà lệnh trước chưa quá 1,5s: bỏ qua;
     *  - nhân vật đang đi / đã nhích kể từ lệnh trước: chỉ gửi lại sau 4s.
     * Game không cho đi (định thân / ngủ): không gửi.
     */
    private var _mv:Object = null;             // lệnh đi gần nhất {x, y, stop, at, fx, fy, map}
    private function moveTo(map:int, x:int, y:int, stop:int, src:String = "khác"):Boolean {
        var now:int = getTimer(), m:Object = me();
        if (charFixed()) return false;
        if (_mv != null && _mv.map == map && Math.abs(x - _mv.x) + Math.abs(y - _mv.y) <= 1 && stop == _mv.stop) {
            if (now - _mv.at < 1500) return false;
            // đang đi thật (trạng thái walk, hoặc vừa đổi ô trong 1s): chưa gửi lại; lệnh trước đã chết (đứng yên) thì gửi lại được
            var moving:Boolean = m.getStatus() == "walk" || (now - _lastMoveAt < 1000 && (m.tile_x != _mv.fx || m.tile_y != _mv.fy));
            if (moving && now - _mv.at < 4000) return false;
        }
        if (!walkCmd(map + "," + x + "," + y + "," + stop, null, src, x, y)) return false;
        _mv = { x: x, y: y, stop: stop, at: now, fx: m.tile_x, fy: m.tile_y, map: map, diag: false };
        return true;
    }
    /** chẩn đoán: gửi lệnh đi 1s mà không nhích -> ghi 1 dòng (tối đa 1 dòng / 10s) */
    private var _mvDiagAt:int = -60000;
    private var _lastMoveAt:int = 0, _lmX:int = -1, _lmY:int = -1;   // lần cuối nhân vật đổi ô
    private function trackMove(now:int):void {
        var m:Object = me();
        if (m && (m.tile_x != _lmX || m.tile_y != _lmY)) { _lmX = m.tile_x; _lmY = m.tile_y; _lastMoveAt = now; }
    }
    private function moveDiag(now:int):void {
        if (_mv == null || _mv.diag || now - _mv.at < 1000 || now - _mv.at > 3000) return;
        var m:Object = me();
        if (m.tile_x != _mv.fx || m.tile_y != _mv.fy) { _mv.diag = true; return; }
        _mv.diag = true;
        if (now - _mvDiagAt < 10000) return;
        _mvDiagAt = now;
        var d:Number = tileDist(m, _mv.x, _mv.y);
        _emit("train", "info kẹt đi: đích (" + _mv.x + "," + _mv.y + ") cách " + d.toFixed(1) + " ô, dừng " + _mv.stop + "px; trạng thái=" + m.getStatus()
              + ", khống chế=" + (charFixed() ? "có" : "không") + ", chiêu gần nhất " + (now - _lastSkill) + "ms trước, hỗ trợ " + (now - _lastSupport)
              + "ms trước, đánh thường " + (now - _lastHit) + "ms trước, nhặt=" + (_pp ? "#" + _pp.bag.id : _pickBag ? "#" + _pickBag.id : "-"));
    }
    /** đang có lệnh đi chưa tới nơi (dùng để không ra buff / chiêu làm đứng lại giữa đường) */
    private function moving(now:int):Boolean {
        if (me().getStatus() == "walk") return true;
        if (_mv == null || tileDist(me(), _mv.x, _mv.y) <= 2) return false;
        // vừa gửi lệnh đi (chưa kịp bước), hoặc còn đang trên đường: vừa đổi ô trong 1,5s (trạng thái "walk" có lúc nháy sang khác giữa 2 đoạn đường)
        return now - _mv.at < 1500 || now - _lastMoveAt < 1500;
    }

    private function revive():void {
        var ba:ByteArray = new ByteArray();
        ba.writeByte(1);
        ba.writeDouble(getTimer());
        _c.NetWorkManager.sendMsg(20075, ba);
        _emit("train", "revive lần " + (_reviveTries + 1));
    }

    private function pipe(name:String, body:Object = null):void {
        _c.PipeManager.sendMsg(name, body);
    }

    // ------------------------------------------------------------------ tiện ích

    private function mapName(id:int):String {
        try { if (_c.MapTransManager) return String(_c.MapTransManager.getSceneNameBySceneId(id)); } catch (e:Error) { }
        return "";
    }

    private function isFuben(id:int):Boolean {
        try { return _c.MapTransManager && int(_c.MapTransManager.getMapRes(id).isFuben) == 1; } catch (e:Error) { }
        return false;
    }

    private static function enc(s:String):String { return encodeURIComponent(s); }

    private function gi():Object { return _c.GameInstance; }

    private function me():Object {
        try { return gi().mainChar; } catch (e:Error) { }
        return null;
    }

    private function curMap():int {
        try {
            var s:Object = gi().scene;
            if (s && s.mapConfig) return s.mapConfig.mapID;
        } catch (e:Error) { }
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

    private function setState(s:String, why:String):void {
        _state = s;
        _emit("train", "state " + s + " " + why);
    }

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
        for each (var p:String in String(s).split(",")) if (p != "") out.push(int(p));
        return out;
    }
}
}
