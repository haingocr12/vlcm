package {
import flash.display.Loader;
import flash.display.Sprite;
import flash.events.Event;
import flash.events.IOErrorEvent;
import flash.events.SecurityErrorEvent;
import flash.events.TimerEvent;
import flash.events.UncaughtErrorEvent;
import flash.external.ExternalInterface;
import flash.net.URLRequest;
import flash.system.ApplicationDomain;
import flash.system.Security;
import flash.utils.ByteArray;
import flash.utils.Timer;
import flash.utils.getTimer;
import flash.utils.setTimeout;

/**
 * VlcmLoader: nạp TGameLoader.swf, tự chọn kênh và nhân vật ở tầng ActionScript.
 *
 * Giao tiếp với host (ExternalInterface):
 *   SWF -> host  vlcm_get_config()            trả <object> gồm:
 *                    url      String  link TGameLoader.swf đầy đủ (kèm query)
 *                    query    String  (tùy chọn) nối vào url bằng ? hoặc &
 *                    line     Number  lineID cần vào; 0 = để người chơi tự chọn
 *                    charName String  tên nhân vật; rỗng = để người chơi tự chọn
 *                    serverId Number  (tùy chọn) số server; tên dạng "[x]tên" vẫn khớp theo phần sau ]
 *   SWF -> host  vlcm_event(name, detail)     cả hai là String, xem danh sách sự kiện bên dưới
 *   host -> SWF  vlcm_state()                 trả trạng thái hiện tại (String)
 *   host -> SWF  vlcm_command(cmd, args)      lệnh train (xem VlcmTrain.as): where, status, train_start, train_stop,
 *                                             skills, bag_names.
 *                                             Trả "ok ..." hoặc "err ...".
 *
 * Sự kiện (name / detail):
 *   status          mô tả ngắn, ví dụ "sandbox=localTrusted", "state=waiting_lines"
 *   classes         các lớp đã tìm được, mỗi dòng "Tên\ttên.đầy.đủ"
 *   line_not_found  danh sách kênh, mỗi dòng "id\ttên\tsốNgười"
 *   char_not_found  danh sách nhân vật, mỗi dòng "roleID\ttên\tcấp\tphái"
 *   entered_game    "roleID\ttên\tlineID"
 *   disconnected    ""  (lineSocket mất kết nối, báo một lần mỗi lần rớt; SWF không đăng ký lại
 *                        handler vì ReloginManager của game tự chọn lại kênh và nhân vật cũ)
 *   error           mô tả lỗi
 *   train           sự kiện của phần train: "start ...", "stop ...", "state ...", "status ...", "revive ...", "error ..."
 */
[SWF(width="800", height="600", frameRate="30", backgroundColor="#FFFFFF")]
public class VlcmLoader extends Sprite {

    // Tên đầy đủ (theo tool, bản program526). Nếu tên nào không còn đúng sau khi game cập nhật,
    // loader tìm theo tên ngắn qua getQualifiedDefinitionNames() và báo tên tìm được trong sự kiện "classes".
    private static const KNOWN:Object = {
        NetWorkManager: "com.tgame.manager::NetWorkManager",
        GameConfig:     "com.tgame.common::GameConfig",
        GameState:      "com.tgame.common::GameState",
        ProcessManager: "com.tgame.manager::ProcessManager",
        FacadeManager:  "com.tgame.manager::FacadeManager",
        PipeConstants:  "com.tgame::PipeConstants",
        LoginRoleVO:    "com.tgame.common.vo.login::LoginRoleVO",
        HeadResChange:  "com.tgame.common.staticdata::HeadResChange",
        // dùng cho train
        GameInstance:              "com.tgame.common::GameInstance",
        PipeManager:               "com.tgame.manager::PipeManager",
        MainCharSeachPathManager:  "com.tgame.manager::MainCharSeachPathManager",
        MapTransManager:           "com.tgame.common.res.map::MapTransManager",
        ExchangePositionManager:   "com.tgame.manager::ExchangePositionManager",
        CDFaceManager:             "com.zcp.manager::CDFaceManager",
        GoodsResManager:           "com.tgame.common.res.goods::GoodsResManager",
        // dùng cho tiện ích (VlcmUtil): hàm gửi của chính game
        Item_MsgSenderProxy:       "com.tgame.moudels.Item.model::Item_MsgSenderProxy",
        MainUI_MsgSendProxy:       "com.tgame.moudels.mainui.model::MainUI_MsgSendProxy",
        ItemFace:                  "com.tgame.common.ui.componets::ItemFace",
        // đi tới túi đồ như game (callback tới nơi -> nhặt); đóng cửa sổ như phím Esc / nút Hủy
        MoveCallBack:              "com.zcp.engine.vo.move::MoveCallBack",
        Engine_MsgSenderProxy:     "com.tgame.moudels.engine.model::Engine_MsgSenderProxy",
        Afk_MsgSenderProxy:        "com.tgame.moudels.afk.model::Afk_MsgSenderProxy",
        FPanel:                    "com.fireice.panel::FPanel",
        FCloseEvent:               "com.fireice.event::FCloseEvent",
        BasePanel:                 "com.tgame.common.ui.componets::BasePanel",
        POPWindowManager:          "com.tgame.common.ui.componets::POPWindowManager",
        // phó bản
        Team_MsgSenderProxy:       "com.tgame.moudels.team.model::Team_MsgSenderProxy",
        BaseEvent:                 "com.zcp.common.events::BaseEvent",
        FightManager:              "com.tgame.manager::FightManager",
        Fight_MsgSenderProxy:      "com.tgame.moudels.fight.model::Fight_MsgSenderProxy",
        BowArrow_MsgSenderProxy:   "com.tgame.moudels.bowArrow.model::BowArrow_MsgSenderProxy",
        Mount_MsgSenderProxy:      "com.tgame.moudels.mount.model::Mount_MsgSenderProxy",
        MainChar_MsgSenderProxy:   "com.tgame.moudels.mainchar.model::MainChar_MsgSenderProxy",
        OtherConst:                "com.tgame.common::OtherConst"     // [đoán] tên gói; sai thì loader tìm theo tên ngắn (JUMP_MAX_DIS)
    };
    private static const REQUIRED:Array = ["NetWorkManager", "GameConfig", "GameState",
        "ProcessManager", "FacadeManager", "PipeConstants", "LoginRoleVO"];
    // Thiếu lớp OPTIONAL không chặn đăng nhập; lệnh train sẽ báo lỗi nếu cần tới.
    private static const OPTIONAL:Array = ["HeadResChange", "GameInstance", "PipeManager",
        "MainCharSeachPathManager", "MapTransManager", "ExchangePositionManager", "CDFaceManager", "GoodsResManager",
        "Item_MsgSenderProxy", "MainUI_MsgSendProxy", "ItemFace",
        "MoveCallBack", "FPanel", "FCloseEvent", "BasePanel", "POPWindowManager",
        "Team_MsgSenderProxy", "BaseEvent", "FightManager",
        "Fight_MsgSenderProxy", "BowArrow_MsgSenderProxy", "Mount_MsgSenderProxy", "MainChar_MsgSenderProxy",
        "Engine_MsgSenderProxy", "Afk_MsgSenderProxy", "OtherConst"];
    private static const TRAIN_NEEDS:Array = ["GameInstance", "PipeManager", "MainCharSeachPathManager", "NetWorkManager"];

    private static const OP_LINES:int = 40300;
    private static const OP_GET_ROLES:int = 10013;
    private static const OP_ROLES:int = 10014;
    private static const OP_ENTERED:int = 10022;
    private static const CTX_PREFIX:String = "VlcmLoader_";   // context riêng, không trùng của game
    private static const WATCH_MS:int = 2000;     // chu kỳ kiểm tra lineSocket.connected
    private static const SINGLE_LINE_WAIT:int = 3000;  // 1 kênh: chờ game tự chọn trước khi tự gửi 10013

    // Thời gian chờ tối đa (ms)
    private static const T_CLASSES:int = 180000;  // tải + giải mã TGame.tse (~6 MB)
    private static const T_LINES:int = 90000;     // từ lúc đăng ký tới khi nhận 40300
    private static const T_ROLES:int = 30000;     // từ lúc gửi 10013 tới khi nhận 10014
    private static const T_ENTER:int = 90000;     // từ lúc chọn nhân vật tới khi nhận 10022

    private var _url:String;
    private var _line:int;
    private var _charName:String;
    private var _serverId:int;

    private var _loader:Loader;
    private var _dom:ApplicationDomain;
    private var _cls:Object = {};
    private var _clsNames:Object = {};
    private var _registered:Object = {};
    private var _state:String = "init";
    private var _deadline:int = 0;
    private var _chosenRole:Object;
    private var _chosenLine:int = -1;
    private var _uncaught:int = 0;
    private var _singleLine:Object;   // kênh duy nhất đang chờ game tự chọn
    private var _singleLineAt:int = 0;
    private var _watch:Timer;
    private var _up:Boolean = false;
    private var _train:VlcmTrain;

    public function VlcmLoader() {
        Security.allowDomain("*");
        Security.allowInsecureDomain("*");
        if (stage) start(); else addEventListener(Event.ADDED_TO_STAGE, onAdded);
    }

    private function onAdded(e:Event):void {
        removeEventListener(Event.ADDED_TO_STAGE, onAdded);
        start();
    }

    // ------------------------------------------------------------------ khởi động

    private function start():void {
        if (!ExternalInterface.available) { return; }   // không có host thì không làm gì
        try {
            ExternalInterface.addCallback("vlcm_state", function():String { return _state; });
            ExternalInterface.addCallback("vlcm_command", onCommand);
        } catch (e:Error) { }
        emit("status", "sandbox=" + Security.sandboxType);

        var cfg:Object = null;
        try { cfg = ExternalInterface.call("vlcm_get_config"); } catch (e:Error) { }
        if (cfg == null || !cfg.url) { fail("vlcm_get_config không trả về url"); return; }

        _url = String(cfg.url);
        if (cfg.query) _url += (_url.indexOf("?") < 0 ? "?" : "&") + String(cfg.query);
        _line = int(cfg.line);
        _charName = clean(cfg.charName ? String(cfg.charName) : "");
        _serverId = int(cfg.serverId);

        _loader = new Loader();
        _loader.contentLoaderInfo.addEventListener(Event.COMPLETE, onLoaded);
        _loader.contentLoaderInfo.addEventListener(IOErrorEvent.IO_ERROR, onLoadError);
        _loader.contentLoaderInfo.addEventListener(SecurityErrorEvent.SECURITY_ERROR, onLoadError);
        if (_loader.hasOwnProperty("uncaughtErrorEvents"))
            _loader["uncaughtErrorEvents"].addEventListener(UncaughtErrorEvent.UNCAUGHT_ERROR, onUncaught);
        _loader.load(new URLRequest(_url));   // không truyền LoaderContext, giống tool

        setState("loading", T_CLASSES);
        addEventListener(Event.ENTER_FRAME, onFrame);
    }

    private function onLoaded(e:Event):void {
        // TGameLoader cần stage. Thêm content thẳng vào stage như tool; nếu bị chặn thì thêm Loader.
        try { stage.addChild(_loader.content); }
        catch (err:Error) { stage.addChild(_loader); }
        if (_state == "loading") setState("waiting_classes", T_CLASSES);
    }

    private function onLoadError(e:Event):void {
        fail("không tải được TGameLoader: " + e.toString());
    }

    private function onUncaught(e:UncaughtErrorEvent):void {
        if (++_uncaught <= 10) emit("status", "uncaught: " + String(e.error));
    }

    // ------------------------------------------------------------------ vòng lặp mỗi frame

    private function onFrame(e:Event):void {
        if (_deadline > 0 && getTimer() > _deadline) {
            fail("hết thời gian chờ ở trạng thái " + _state);
            return;
        }
        if (_state == "waiting_single_line") {
            if (getTimer() >= _singleLineAt) chooseLine(_singleLine, "game không tự chọn kênh duy nhất");
            return;
        }
        if (_state != "waiting_classes") return;
        if (_dom == null) {
            // Có thể ném SecurityError khi game chưa allowDomain; thử lại frame sau.
            try { _dom = _loader.contentLoaderInfo.applicationDomain; } catch (err:SecurityError) { return; }
        }
        tryResolveClasses();
    }

    private function tryResolveClasses():void {
        try {
            if (!_dom.hasDefinition(KNOWN.NetWorkManager)) return;   // TGame chưa nạp xong
        } catch (e:SecurityError) {
            return;   // allowDomain của game có thể chưa chạy; thử lại frame sau
        }

        var names:Array = null;
        var missing:Array = [];
        var all:Array = REQUIRED.concat(OPTIONAL);
        for each (var shortName:String in all) {
            var full:String = KNOWN[shortName];
            var c:Class = null;
            try {
                if (full && _dom.hasDefinition(full)) c = _dom.getDefinition(full) as Class;
                if (!c) {   // tên trong KNOWN không còn đúng: tìm theo tên ngắn
                    if (names == null) names = listDefinitionNames();
                    full = findByShortName(names, shortName);
                    if (full && _dom.hasDefinition(full)) c = _dom.getDefinition(full) as Class;
                    if (c) emit("status", shortName + " không có ở tên trong KNOWN, dùng " + full);
                }
            } catch (e:SecurityError) { return; }   // thử lại frame sau
            if (c) { _cls[shortName] = c; _clsNames[shortName] = full; }
            else if (REQUIRED.indexOf(shortName) >= 0) missing.push(shortName);
        }

        var report:Array = [];
        for (var k:String in _clsNames) report.push(k + "\t" + _clsNames[k]);
        emit("classes", report.join("\n"));

        if (missing.length > 0) {
            fail("không tìm thấy lớp: " + missing.join(", ") + " (sửa tên đầy đủ trong KNOWN)");
            return;
        }
        registerHandlers();
    }

    private function listDefinitionNames():Array {
        var out:Array = [];
        try {
            var v:Object = _dom["getQualifiedDefinitionNames"]();   // Flash Player 11.3+
            for each (var n:String in v) out.push(n);
        } catch (e:Error) {
            emit("status", "getQualifiedDefinitionNames lỗi: " + e.message);
        }
        return out;
    }

    private static function findByShortName(names:Array, shortName:String):String {
        var hits:Array = [];
        for each (var n:String in names) {
            if (n == shortName || n.substr(-(shortName.length + 2)) == "::" + shortName) hits.push(n);
        }
        if (hits.length == 0) return null;
        for each (var h:String in hits) if (h.indexOf("com.tgame") == 0) return h;
        return hits[0];
    }

    // ------------------------------------------------------------------ đăng ký gói tin

    private function registerHandlers():void {
        // Đăng ký 40300 và 10014 cùng lúc: 10014 có thể về rất nhanh sau 10013.
        register(OP_LINES, onLines);
        register(OP_ROLES, onRoles);
        register(OP_ENTERED, onEntered);
        setState("waiting_lines", T_LINES);
    }

    private function register(op:int, handler:Function):void {
        _cls.NetWorkManager.registerMsg(op, handler, CTX_PREFIX + op);
        _registered[op] = true;
    }

    private function unregister(op:int):void {
        if (!_registered[op]) return;
        _registered[op] = false;
        // removeMsg của game ném TypeError nếu opcode không còn observer nào
        try { _cls.NetWorkManager.removeMsg(op, CTX_PREFIX + op); } catch (e:Error) { }
    }

    // ------------------------------------------------------------------ 40300: danh sách kênh

    private function onLines(n:Object):void {
        unregister(OP_LINES);   // lần 40300 sau là CHANGE_LINE trong game, không được đụng tới
        var lines:Array;
        try { lines = readLines(n.body as ByteArray); }
        catch (e:Error) { fail("đọc 40300 lỗi: " + e.message); return; }

        if (lines.length == 1) {
            // Theo tài liệu, game tự chọn khi chỉ có 1 kênh (chưa xác minh). Chờ SINGLE_LINE_WAIT:
            // nếu 10014 về thì game đã tự chọn; nếu không thì tự chọn như tool.
            _chosenLine = lines[0].id;
            _singleLine = lines[0];
            _singleLineAt = getTimer() + SINGLE_LINE_WAIT;
            if (_line > 0 && _line != lines[0].id)
                emit("status", "chỉ có 1 kênh (id=" + lines[0].id + "), không phải kênh " + _line + "; vẫn vào kênh này");
            setState("waiting_single_line", 0);
            return;
        }
        if (_line <= 0) { setState("manual_line", 0); return; }

        var chosen:Object = null;
        for each (var l:Object in lines) if (l.id == _line) { chosen = l; break; }
        if (chosen == null) {
            // Không rơi về kênh đầu như tool: báo host và để người chơi tự chọn.
            var list:Array = [];
            for each (l in lines) list.push(l.id + "\t" + clean(l.name) + "\t" + l.count);
            emit("line_not_found", list.join("\n"));
            setState("manual_line", 0);
            return;
        }

        chooseLine(chosen, null);
    }

    private function chooseLine(l:Object, reason:String):void {
        if (reason) emit("status", reason + ", tự chọn kênh " + l.id);
        try {
            selectLine(l);
            _cls.NetWorkManager.sendMsg(OP_GET_ROLES, new ByteArray(), _cls.NetWorkManager.loginSocket);
        } catch (e:Error) { fail("chọn kênh lỗi: " + e.message); return; }
        _chosenLine = l.id;
        setState("waiting_roles", T_ROLES);
    }

    private static function bareName(n:String):String {
        var b:int = n.indexOf("]");
        return n.charAt(0) == "[" && b > 0 ? n.substr(b + 1) : n;
    }

    private static function readLines(body:ByteArray):Array {
        var out:Array = [];
        body.position = 4;   // 4 byte đầu là opcode; handler của game đã đọc trước và đẩy position đi
        try {
            var count:int = body.readUnsignedByte();
            for (var i:int = 0; i < count; i++) {
                var o:Object = {};
                o.id = body.readByte();
                o.host = body.readUTF();
                o.port = body.readUTF();
                o.name = body.readUTF();
                o.count = body.readInt();
                out.push(o);
            }
        } finally {
            body.position = 4;
        }
        return out;
    }

    // Theo selectLine() của game/tool. Giữ nguyên chuỗi đọc được như game; cổng có thể là
    // danh sách "9002,9003": gán cả mảng, lấy cổng đầu ra lineServerPort (game thử cổng sau khi lỗi).
    private function selectLine(l:Object):void {
        var GC:Object = _cls.GameConfig;
        GC.lineID = l.id;
        GC.lineName = l.name;
        GC.lineServerIP = l.host;
        GC.lineServerPortArr = clean(l.port).split(",").map(
            function(p:*, i:int, a:Array):int { return int(Number(p)); });
        GC.lineServerPort = GC.lineServerPortArr.shift();
    }

    // ------------------------------------------------------------------ 10014: danh sách nhân vật

    private function onRoles(n:Object):void {
        unregister(OP_ROLES);
        var roles:Array;
        try { roles = readRoles(n.body as ByteArray); }
        catch (e:Error) { fail("đọc 10014 lỗi: " + e.message); return; }

        var role:Object = null;
        if (_charName != "") {
            // Tên có thể mang tiền tố máy chủ "[x]" ở một phía (panel lưu tên hiển thị trong game): so phần sau "]".
            var want:String = bareName(_charName);
            for each (var r:Object in roles) {
                if (bareName(clean(r.name)) == want) { role = r; break; }
            }
        }
        // Tài khoản chỉ có 1 nhân vật: luôn chọn nhân vật đó (tên đã lưu khớp hay không). Nhiều nhân vật thì để người chơi chọn.
        if (role == null && roles.length == 1) {
            if (_charName != "") emit("status", "không khớp tên " + _charName + ", tài khoản chỉ có 1 nhân vật nên tự chọn");
            role = roles[0];
        }
        if (role == null) {
            if (_charName != "") {
                var list:Array = [];
                for each (r in roles) list.push(r.id + "\t" + clean(r.name) + "\t" + r.level + "\t" + r.party);
                emit("char_not_found", list.join("\n"));
            }
            setState("manual_role", 0);
            return;
        }
        if (!_cls.HeadResChange) {
            fail("thiếu lớp HeadResChange, không dựng được LoginRoleVO; để người chơi tự chọn");
            return;
        }

        try {
            var vo:Object = new (_cls.LoginRoleVO as Class)();
            vo.roleID = role.id;
            vo.nickName = role.name;   // chuỗi gốc như game đọc
            vo.headImg = _cls.HeadResChange.headResChangeToString(role.head);
            vo.level = role.level;
            vo.partyID = role.party;
            // Đường giao diện của game để sex = 0; tool gán theo phái. Theo tool.
            try { vo.sex = vo.getSex(); } catch (e:Error) { }
            _chosenRole = role;

            // Thứ tự theo tool (đã chạy được): enterLine() trước, killFacade() sau.
            // Login_LoginMediator.selecttRole() của game làm ngược lại (kill trước).
            _cls.GameConfig.loginRoleVO = vo;
            _cls.GameState.hasSelectedChar = true;
            _cls.ProcessManager.enterLine();
            _cls.FacadeManager.killFacade(_cls.PipeConstants.STARTUP_LOGIN);
        } catch (e:Error) { fail("vào game lỗi: " + e.message); return; }
        setState("entering", T_ENTER);
    }

    private static function readRoles(body:ByteArray):Array {
        var out:Array = [];
        body.position = 4;
        try {
            var count:int = body.readUnsignedByte();
            for (var i:int = 0; i < count; i++) {
                var o:Object = {};
                o.id = body.readInt();
                o.head = body.readByte();
                o.name = body.readUTF();
                o.level = body.readShort();
                o.party = body.readByte();
                out.push(o);
            }
        } finally {
            body.position = 4;
        }
        return out;
    }

    // ------------------------------------------------------------------ 10022: đã vào game

    private function onEntered(n:Object):void {
        unregister(OP_ENTERED);
        unregister(OP_LINES);
        unregister(OP_ROLES);
        var id:String = _chosenRole ? String(_chosenRole.id) : "";
        var nm:String = _chosenRole ? clean(_chosenRole.name) : "";
        var line:String = _chosenLine >= 0 ? String(_chosenLine) : "";
        emit("entered_game", id + "\t" + nm + "\t" + line);
        setState("in_game", 0);
        removeEventListener(Event.ENTER_FRAME, onFrame);
        startWatch();
    }

    // Như tool: kiểm tra định kỳ NetWorkManager.lineSocket.connected thay vì nghe sự kiện,
    // vì ReloginManager tạo socket mới và listener trên socket cũ sẽ không còn tác dụng.
    private function startWatch():void {
        if (_watch) return;
        _up = lineConnected();
        _watch = new Timer(WATCH_MS);
        _watch.addEventListener(TimerEvent.TIMER, onWatch);
        _watch.start();
    }

    private function lineConnected():Boolean {
        try { var s:Object = _cls.NetWorkManager.lineSocket; return s != null && Boolean(s.connected); }
        catch (err:Error) { }
        return false;
    }

    private function onWatch(e:TimerEvent):void {
        if (lineConnected()) { _up = true; return; }
        // Báo một lần mỗi lần rớt. Không đăng ký lại handler: ReloginManager tự chọn lại kênh và nhân vật cũ.
        if (_up) { _up = false; emit("disconnected", ""); }
    }

    // ------------------------------------------------------------------ lệnh từ host (train)

    private function onCommand(cmd:String, args:String):String {
        try {
            if (_train == null) {
                var missing:Array = [];
                for each (var n:String in TRAIN_NEEDS) if (!_cls[n]) missing.push(n);
                if (missing.length > 0) return "err game chưa sẵn sàng hoặc thiếu lớp: " + missing.join(", ");
                // Sự kiện train phát sau khi lời gọi hiện tại trả về: tránh gọi ExternalInterface lồng
                // bên trong CallFunction của host.
                _train = new VlcmTrain(_cls, function(n:String, d:String):void { setTimeout(emit, 0, n, d); });
            }
            return _train.command(cmd ? cmd : "", args ? args : "");
        } catch (e:Error) {
            return "err " + e.message;
        }
        return "err";
    }

    // ------------------------------------------------------------------ tiện ích

    private function setState(s:String, timeout:int):void {
        _state = s;
        _deadline = timeout > 0 ? getTimer() + timeout : 0;
        emit("status", "state=" + s);
    }

    private function fail(msg:String):void {
        _state = "error";
        _deadline = 0;
        removeEventListener(Event.ENTER_FRAME, onFrame);
        emit("error", msg);
    }

    private static function emit(name:String, detail:String):void {
        try { ExternalInterface.call("vlcm_event", name, detail); } catch (e:Error) { }
    }

    // Cắt từ ký tự \0 đầu tiên (chuỗi server gửi có \0 ở cuối).
    private static function clean(s:String):String {
        if (s == null) return "";
        var i:int = s.indexOf(String.fromCharCode(0));
        return i < 0 ? s : s.substr(0, i);
    }
}
}
