// vlcmpanel.cpp - bang dieu khien train cho nhieu cua so vlcmhost (Win32 thuan, khong ATL).
//
// Tren: danh sach acc (moi vlcmhost dang chay = mot dong).   Duoi trai: DIEU KHIEN + VIP + thong ke dong.
// Duoi phai: HOAT DONG (log) | TRAIN (TOA DO / CAI DAT KHAC / CHUYEN BAI)
//            | TIEN ICH (KY NANG / THUOC / NHAT DO / SUA DO / BAN-HUY / AN TOAN / DI CHUYEN).
// Noi chuyen voi vlcmhost qua \.\pipelcmhost-<acc> (PipeServer.h). Moi pipe chi nhan mot
// ket noi, nen khi panel dang mo thi vlcmctl.exe khong ket noi duoc acc do.
// Cai dat luu o data	rain_<acc>.ini canh exe (UTF-16). Nhat ky ban/huy: dataanhuy_<acc>.log.
//
// Build (MSVC):  cl /EHsc /std:c++17 /utf-8 /O2 /MT vlcmpanel.cpp user32.lib gdi32.lib comctl32.lib shell32.lib
// Build (MinGW): g++ -std=c++17 -municode -mwindows -O2 -static vlcmpanel.cpp -lcomctl32 -o vlcmpanel.exe
// Tham so: vlcmpanel.exe [--hosts acc1,acc2]   (them acc ngoai cac pipe tim duoc)

#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include <commctrl.h>

#include <algorithm>
#include <atomic>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <shellapi.h>
#include <wincrypt.h>

#ifdef _MSC_VER
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif

// ============================================================ tien ich chung

static std::string ToUtf8(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}
static std::wstring FromUtf8(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}
static std::wstring ExeDir() {
    wchar_t b[MAX_PATH]; GetModuleFileNameW(nullptr, b, MAX_PATH);
    std::wstring p = b; size_t s = p.find_last_of(L"\\/");
    return s == std::wstring::npos ? p : p.substr(0, s);
}
static int HexVal(wchar_t c) { return c >= L'0' && c <= L'9' ? c - L'0' : c >= L'A' && c <= L'F' ? c - L'A' + 10 : c >= L'a' && c <= L'f' ? c - L'a' + 10 : -1; }
static std::wstring Decode(const std::wstring& s) {          // giai ma URL (encodeURIComponent cua AS3), UTF-8
    std::string bytes;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == L'%' && i + 2 < s.size() && HexVal(s[i + 1]) >= 0 && HexVal(s[i + 2]) >= 0) {
            bytes += (char)(HexVal(s[i + 1]) * 16 + HexVal(s[i + 2])); i += 2;
        } else bytes += ToUtf8(std::wstring(1, s[i]));
    }
    return FromUtf8(bytes);
}
static std::wstring Encode(const std::wstring& s) {          // ma hoa URL (VlcmTrain giai bang decodeURIComponent)
    std::wstring o; const wchar_t* hex = L"0123456789ABCDEF";
    for (unsigned char c : ToUtf8(s)) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += (wchar_t)c;
        else { o += L'%'; o += hex[c >> 4]; o += hex[c & 15]; }
    }
    return o;
}
static std::map<std::wstring, std::wstring> ParseKV(const std::wstring& s) {
    std::map<std::wstring, std::wstring> kv;
    size_t i = 0;
    while (i < s.size()) {
        size_t j = s.find(L' ', i);
        if (j == std::wstring::npos) j = s.size();
        std::wstring t = s.substr(i, j - i);
        size_t e = t.find(L'=');
        if (e != std::wstring::npos && e > 0) kv[t.substr(0, e)] = Decode(t.substr(e + 1));
        i = j + 1;
    }
    return kv;
}
static int ToInt(const std::wstring& s, int def = 0) {
    if (s.empty()) return def;
    wchar_t* end = nullptr;
    long v = wcstol(s.c_str(), &end, 10);
    return end == s.c_str() ? def : (int)v;
}
static std::wstring Now() {
    SYSTEMTIME t; GetLocalTime(&t);
    wchar_t b[16]; swprintf(b, 16, L"%02d:%02d:%02d", t.wHour, t.wMinute, t.wSecond);
    return b;
}
static bool StartsWith(const std::wstring& s, const wchar_t* p) { return s.rfind(p, 0) == 0; }

// ============================================================ ket noi pipe (luong rieng moi acc)

static const UINT WM_LINK_LINE = WM_APP + 10;   // lParam = LinkMsg*
static const UINT WM_LINK_CONN = WM_APP + 11;   // wParam = 1/0, lParam = LinkMsg*

struct LinkMsg { std::wstring acc, line; };

class Link {
public:
    Link(HWND hwnd, const std::wstring& acc) : hwnd_(hwnd), acc_(acc) {
        stopEv_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        sendEv_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        th_ = std::thread(&Link::Run, this);
    }
    ~Link() {
        SetEvent(stopEv_);
        if (th_.joinable()) th_.join();
        CloseHandle(stopEv_); CloseHandle(sendEv_);
    }
    void Send(const std::wstring& line) {
        if (!connected_) return;
        { std::lock_guard<std::mutex> lk(mu_); q_.push_back(ToUtf8(line) + "\n"); }
        SetEvent(sendEv_);
    }
    bool Connected() const { return connected_; }

private:
    void Post(UINT msg, WPARAM wp, const std::wstring& line) {
        auto* m = new LinkMsg{ acc_, line };
        if (!PostMessageW(hwnd_, msg, wp, (LPARAM)m)) delete m;
    }
    void Run() {
        std::wstring name = L"\\\\.\\pipe\\vlcmhost-" + acc_;
        while (WaitForSingleObject(stopEv_, 0) != WAIT_OBJECT_0) {
            HANDLE h = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
            if (h == INVALID_HANDLE_VALUE) {
                // Pipe chi nhan 1 client: ERROR_PIPE_BUSY = vlcmctl (hoac panel khac) dang noi tai khoan nay.
                bool busy = GetLastError() == ERROR_PIPE_BUSY;
                if (busy && !busyNoted_) { busyNoted_ = true; Post(WM_LINK_CONN, 2, L""); }
                if (WaitForSingleObject(stopEv_, 2000) == WAIT_OBJECT_0) break;
                continue;
            }
            { std::lock_guard<std::mutex> lk(mu_); q_.clear(); }
            busyNoted_ = false;
            connected_ = true;
            Post(WM_LINK_CONN, 1, L"");
            Serve(h);
            connected_ = false;
            CloseHandle(h);
            Post(WM_LINK_CONN, 0, L"");
            if (WaitForSingleObject(stopEv_, 1000) == WAIT_OBJECT_0) break;
        }
    }
    void Serve(HANDLE h) {
        HANDLE rdEv = CreateEventW(nullptr, TRUE, FALSE, nullptr), wrEv = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        char buf[4096];
        std::string pending;
        OVERLAPPED rov = {};
        bool reading = false, alive = true;
        while (alive) {
            if (!reading) {
                ZeroMemory(&rov, sizeof(rov)); rov.hEvent = rdEv; ResetEvent(rdEv);
                if (!ReadFile(h, buf, sizeof(buf), nullptr, &rov) && GetLastError() != ERROR_IO_PENDING) break;
                reading = true;
            }
            HANDLE hs[3] = { stopEv_, rdEv, sendEv_ };
            DWORD w = WaitForMultipleObjects(3, hs, FALSE, INFINITE);
            if (w == WAIT_OBJECT_0) break;
            if (w == WAIT_OBJECT_0 + 1) {
                DWORD n = 0; reading = false;
                if (!GetOverlappedResult(h, &rov, &n, FALSE) || n == 0) break;
                pending.append(buf, n);
                size_t p;
                while ((p = pending.find('\n')) != std::string::npos) {
                    std::string one = pending.substr(0, p); pending.erase(0, p + 1);
                    if (!one.empty() && one.back() == '\r') one.pop_back();
                    if (!one.empty()) Post(WM_LINK_LINE, 0, FromUtf8(one));
                }
            }
            for (;;) {
                std::string out;
                { std::lock_guard<std::mutex> lk(mu_); if (q_.empty()) break; out.swap(q_.front()); q_.pop_front(); }
                OVERLAPPED ov = {}; ov.hEvent = wrEv; ResetEvent(wrEv); DWORD n = 0;
                if (!WriteFile(h, out.data(), (DWORD)out.size(), nullptr, &ov)) {
                    if (GetLastError() != ERROR_IO_PENDING || WaitForSingleObject(wrEv, 5000) != WAIT_OBJECT_0) { alive = false; break; }
                }
                if (!GetOverlappedResult(h, &ov, &n, FALSE)) { alive = false; break; }
            }
        }
        if (reading) { CancelIo(h); DWORD n = 0; GetOverlappedResult(h, &rov, &n, TRUE); }
        CloseHandle(rdEv); CloseHandle(wrEv);
    }

    HWND hwnd_;
    std::wstring acc_;
    HANDLE stopEv_, sendEv_;
    std::thread th_;
    std::mutex mu_;
    std::deque<std::string> q_;
    std::atomic<bool> connected_{ false };
    bool busyNoted_ = false;             // chi dung trong thread Run
};

// ============================================================ du lieu acc + cai dat

struct TrainPoint { int map = 0, x = 0, y = 0, r = 5; bool on = true; std::wstring name; };

static const wchar_t* DEFAULT_PICKLIST =
    L"[Mảnh],[Đồng],[Hoa Hồng Đỏ],[Hoa Hồng Vàng],[Hoa Hồng Lam],[Hoa Hồng Lục],[Hoa Hồng Trắng],[Hoa Hồng Đen],"
    L"[Mảnh Vân Thạch],[Thông Lam],[Chân Khí Đơn],[Tôn Chân Khí],[Chân Long Đồng Nhân],[Thông Lục],[Đá May Mắn],"
    L"[Kinh Mạch Đồng Nhân],[Đá Kim Cương],[Đá Tinh Luyện],[Đá Thăng Cấp],[Bồ Đề Đơn],[Bích Linh Đơn],[Luyện Cốt Đơn],"
    L"[Thưởng Phạt Lệnh],[Đá Dưỡng Thiên],[Kinh Nghiệm Đơn x2],[Phỉ Thúy (loại 1)],[Hồng Ngọc (loại 1)],[Mã Não (loại 1)],"
    L"[Bùa Mở Rương],[Tiến Cấp Phù],[Long Thú Cân],[Bia Ám Khí],[Tụ Linh Châu],[Mảnh nguyên liệu cao cấp],[Tụ Pháp Đơn],"
    L"[Exp Thú Cưỡi x2],[Đại Chân Khí],[Chân Khí x2],[Bách Lộ],[Liên Lộ],[Bích Đơn],[Nông Đơn],[Vân Hoàn],[Bách Hoàn],"
    L"[Tuyết Hoàn],[Thủy Đơn],[Thạch Lộ],[Tục Mệnh Đơn],[Hồi Thể Đơn],[Thông Tím],[Đá Ám Khí],[Thăng Đoạn Thạch],"
    L"[Đá Tăng Tốc-Tiêu],[Tư Chất Đơn],[Long Châu],[Quả Sung]";

// Thuoc (Drugs.xml): thu tu nho -> lon
struct Drug { int id; const wchar_t* name; bool hp; };
static const Drug DRUGS[] = {
    { 30101, L"Quy Đơn", true }, { 30102, L"Thủy Đơn", true }, { 30103, L"Bích Đơn", true }, { 30104, L"Thần Đơn", true },
    { 30201, L"Ninh Hoàn", false }, { 30202, L"Tuyết Hoàn", false }, { 30203, L"Vân Hoàn", false }, { 30204, L"Bách Hoàn", false },
};
static const int NDRUGS = sizeof(DRUGS) / sizeof(DRUGS[0]);

// Quy tac ban/huy (gui len dang "act=..;real=..;kind=..;q=..;s=..;g=..;p=..;pos=..;lock=..;names=A|B")
struct Rule {
    int act = 1;             // 0 ban, 1 huy
    bool real = false;       // false = chay thu (chi ghi log)
    int kind = 0;            // 0 trang bi theo thuoc tinh, 1 vat pham theo ten
    int q = -1, s = -1, g = -1;
    std::wstring sects, slots, names;   // names: cach nhau dau phay
    int lock = 0;            // 0 bat ky, 1 chi do khoa, 2 chi do khong khoa
};

static std::wstring NumList(const std::wstring& s) {          // chi giu so va dau phay
    std::wstring o; for (wchar_t c : s) if ((c >= L'0' && c <= L'9') || c == L',') o += c; return o;
}
static std::vector<std::wstring> SplitNames(const std::wstring& s) {
    std::vector<std::wstring> v; size_t i = 0;
    while (i <= s.size()) {
        size_t e = s.find(L',', i); if (e == std::wstring::npos) e = s.size();
        std::wstring t = s.substr(i, e - i);
        for (auto& c : t) if (c == L'|' || c == L';' || c == L'\r' || c == L'\n' || c == L'[' || c == L']') c = L' ';
        size_t a = t.find_first_not_of(L' '), b = t.find_last_not_of(L' ');
        if (a != std::wstring::npos) v.push_back(t.substr(a, b - a + 1));
        i = e + 1;
    }
    return v;
}
static std::wstring RuleLine(const Rule& r) {
    std::wstring names;
    for (auto& n : SplitNames(r.names)) names += (names.empty() ? L"" : L"|") + n;
    return std::wstring(L"act=") + (r.act == 0 ? L"sell" : L"destroy") + L";real=" + (r.real ? L"1" : L"0")
        + L";kind=" + (r.kind == 0 ? L"equip" : L"item") + L";q=" + std::to_wstring(r.q) + L";s=" + std::to_wstring(r.s)
        + L";lv=" + std::to_wstring(r.g) + L";p=" + NumList(r.sects) + L";pos=" + NumList(r.slots)
        + L";lock=" + (r.lock == 1 ? L"locked" : r.lock == 2 ? L"unlocked" : L"any") + L";names=" + names;
}
static bool ParseRuleLine(const std::wstring& line, Rule& r) {
    size_t i = 0; bool any = false;
    while (i <= line.size()) {
        size_t e = line.find(L';', i); if (e == std::wstring::npos) e = line.size();
        std::wstring part = line.substr(i, e - i); size_t eq = part.find(L'=');
        if (eq != std::wstring::npos) {
            std::wstring k = part.substr(0, eq), v = part.substr(eq + 1);
            any = true;
            if (k == L"act") r.act = v == L"sell" ? 0 : 1;
            else if (k == L"real") r.real = v == L"1";
            else if (k == L"kind") r.kind = v == L"item" ? 1 : 0;
            else if (k == L"q") r.q = ToInt(v, -1);
            else if (k == L"s") r.s = ToInt(v, -1);
            else if (k == L"lv") r.g = ToInt(v, -1);           // cap nhan vat can de mac (g cu = cap trang bi: bo)
            else if (k == L"p") r.sects = v;
            else if (k == L"pos") r.slots = v;
            else if (k == L"lock") r.lock = v == L"locked" ? 1 : v == L"unlocked" ? 2 : 0;
            else if (k == L"names") { std::wstring n = v; for (auto& c : n) if (c == L'|') c = L','; r.names = n; }
        }
        i = e + 1;
    }
    return any;
}
struct IdName { int id; const wchar_t* name; };
static const IdName SECTS[] = { { 1, L"Thiếu Lâm" }, { 2, L"Toàn Chân" }, { 3, L"Cổ Mộ" }, { 4, L"Đào Hoa" } };
static const int NSECTS = 4;
static const IdName SLOTS[] = { { 1, L"Vũ khí" }, { 2, L"Vũ khí thú cưỡi" }, { 3, L"Áo" }, { 4, L"Uyển" }, { 5, L"Đai lưng" }, { 6, L"Giày" },
                                { 7, L"Mũ" }, { 8, L"Dây chuyền" }, { 9, L"Nhẫn" }, { 10, L"Vòng tay" }, { 11, L"Ngọc bội" }, { 12, L"Áo choàng" } };
static const int NSLOTS = 12;
static std::wstring IdNames(const std::wstring& ids, const IdName* tab, int n) {
    std::wstring o; size_t i = 0; std::wstring l = NumList(ids);
    while (i < l.size()) {
        size_t e = l.find(L',', i); if (e == std::wstring::npos) e = l.size();
        int id = ToInt(l.substr(i, e - i), -1);
        std::wstring nm = std::to_wstring(id);
        for (int k = 0; k < n; ++k) if (tab[k].id == id) nm = tab[k].name;
        if (id >= 0) o += (o.empty() ? L"" : L", ") + nm;
        i = e + 1;
    }
    return o;
}
static const wchar_t* QNAMES[] = { L"Không xét", L"Trắng", L"Từ Lam trở xuống", L"Từ Lục trở xuống", L"Từ Tím trở xuống" };
static std::wstring RuleText(const Rule& r) {
    std::wstring t = std::wstring(r.act == 0 ? L"Bán " : L"Hủy ") + (r.kind == 0 ? L"trang bị: " : L"vật phẩm: ");
    if (r.kind == 1) return t + r.names;
    std::vector<std::wstring> c;
    if (r.q >= 1 && r.q <= 4) c.push_back(std::wstring(L"màu ") + QNAMES[r.q]);
    if (r.s >= 0) c.push_back(L"sao ≤ " + std::to_wstring(r.s));
    if (r.g >= 0) c.push_back(L"cấp dùng ≤ " + std::to_wstring(r.g));
    if (!NumList(r.sects).empty()) c.push_back(L"phái: " + IdNames(r.sects, SECTS, NSECTS));
    if (!NumList(r.slots).empty()) c.push_back(L"loại: " + IdNames(r.slots, SLOTS, NSLOTS));
    if (r.lock == 1) c.push_back(L"đồ khóa"); else if (r.lock == 2) c.push_back(L"đồ không khóa");
    for (size_t i = 0; i < c.size(); ++i) t += (i ? L", " : L"") + c[i];
    return t;
}
static bool RuleValid(const Rule& r) {
    if (r.kind == 1) return !SplitNames(r.names).empty();
    return r.q >= 1 || r.s >= 0 || r.g >= 0 || !NumList(r.sects).empty() || !NumList(r.slots).empty() || r.lock != 0;
}

struct Config {
    std::vector<TrainPoint> pts;
    int skillMode = 0;           // 0 = nhom ky nang (nhom trong thi dung 5 o dau), 1 = bam phim
    std::wstring skillIds;       // (cu) "51013,51014" - chi dung de chuyen sang nhom ky nang
    struct SkSet { std::wstring g[4]; std::wstring heal, buff; int supmp = 30; };
    SkSet sets[5];
    int trainSet = 0;            // nhom ky nang dung khi train
    std::wstring keys = L"1,2,3";
    int pickMode = 0;            // 0 = khong nhat, 1 = theo danh sach, 2 = nhat tat ca
    std::wstring pickList = DEFAULT_PICKLIST;
    bool tNormal = true, tElite = true, tBoss = false;
    bool restOn = false; int restDeaths = 10; int restMin = 5;
    bool swTimeOn = false; int swMin = 30;
    bool swDeath = false;
    bool enabled = false;        // "Danh quai (Train)" dang tick
    // thuoc
    int potMode = 0;             // 0 = bam phim tat, 1 = thuoc trong tui theo ID
    std::wstring hpkey, mpkey;   // rong = khong dung
    int hp = 40, mp = 20;
    std::wstring drugIds = L"30101,30102,30103,30104,30201,30202,30203,30204";
    // sua do
    bool repairOn = false; int repairType = 1;
    // ban / huy
    bool bhOn = false;
    std::vector<Rule> rules;
    int freeE = 5, freeI = 5;
    bool floorQ = true;          // bao ve do Tim tro len
    int floorS = 1;              // bao ve trang bi cuong hoa tu +N (0 = tat)
    std::wstring protect = L"Mảnh Trận Pháp,Hoa Hồng";
    // di chuyen
    bool tele = true, vllAuto = false;
    // cai dat trong game (SETTING_DATA 0..12) + an hieu ung sinh luc nguoi khac
    bool gs[13] = {};
    bool hpfx = false;
    bool closeUi = true;                     // dong cua so tinh nang trong 20s dau sau khi vao game
    // pho ban: 0 Lien Tram, 1 Thien Quan, 2 Doanh Trai, 3 Phu Tu Tran, 4 Me Cung Tran
    bool pbOn[5] = {};                       // o tick (khong luu: mo panel khong tu tick)
    int pbRange[5] = { 99, 99, 99, 99, 99 }; // pham vi tim quai (o) tung pho ban
    int pbSet[5] = { -1, -1, -1, -1, -1 };   // bo ky nang tung pho ban (-1 = giong Danh quai)
    int npcEnter[5] = { -1, -1, -1, -1, -1 }, npcTotal[5] = { -1, -1, -1, -1, -1 };   // so luot da di / toi da doc tu NPC (hom nay)
    bool tqAfk = true;
    bool tqBossCount = true;                 // Thien Quan: bo qua danh boss o trang thai dem so (boss mang buff bat tu / bao ho)                       // Thien Quan: het vong khong thay quai -> treo may cua game pham vi 99
    int pbRev[5] = { 2, 2, 2, 2, 2 };        // hoi sinh bang hoa hong toi da moi luot
    bool ptJump = true;                      // Phu Tu Tran: nhay khi danh Khoi Khoi (luc boss mang trang thai dac biet)
    bool ptBow = true;                       // Phu Tu Tran: xong thi bat lai cung
    int mcFarm = 0, mcFarmMin = 10;          // Me Cung: dung o tang N (0 = khong) treo danh quai M phut
    int mcFarmBy = 0, mcFarmLz = 300;        // Me Cung: ngung treo khi 0 = het M phut, 1 = dat lien tram X
    bool mcSkip = false;                     // Me Cung: bo qua ai chuot tang 15 + phong than bi (di thang toi cua/cong)
    int tqMinR = 15;                         // Thien Quan: can it nhat bay nhieu hoa moi vao
    bool dtJump = true;                      // Doanh Trai: nhay quanh quai
    int dtJmax = 0;                          // Doanh Trai: tam nhay 0 = theo game (JUMP_MAX_DIS), 1 = 500 (thu, nhu ban cu)
    // moi pho ban: khung gio, dieu kien buff lien tram, dung sau ai N, roi khi khong co quai N phut
    bool pbTimeOn[5] = {}; int pbH1[5] = { 0, 0, 0, 0, 0 }, pbM1[5] = {}, pbH2[5] = { 23, 23, 23, 23, 23 }, pbM2[5] = { 59, 59, 59, 59, 59 };
    bool pbLzOn[5] = {}; int pbLzVal[5] = { 300, 400, 400, 300, 400 }, pbLzMin[5] = { 0, 20, 20, 15, 20 };
    bool pbSfOn[5] = {}; int pbSf[5] = { 0, 3, 15, 0, 0 };
    bool pbNmOn[5] = {}; int pbNm[5] = { 3, 3, 3, 3, 3 };
    // Lien Tram
    bool ltStopOn = true; int ltY = 500; bool ltCont = true;
    bool ltLureOn = true; int ltX = 100; int ltKpp = 2; int ltPick = 0; bool ltSkipBoss = false;
    int pbDone[5] = {};                      // luot da xong hom nay
    std::wstring pbDate;                     // ngay cua pbDone
    int hideLevel = 1;           // giam tai khi an: 1 = tat ve, 2 = tat ve + 15 khung hinh
};

static const int NPB = 5;                  // so pho ban
struct Status {
    bool inGame = false;
    std::wstring state, name, mapname, bh, trip;
    int map = 0, x = 0, y = 0, hp = 0, hpmax = 0, lv = 0, kills = 0, deaths = 0, picked = 0, rest = -1;
    bool vip = false; long long vipexp = 0;
    long long copper = 0, mpick = 0, msold = 0, mrep = 0, since = 0;
    int free = -1, dur = -1;
    std::wstring bhn, render, perf;
    int mlost = 0, prefused = 0;
    std::wstring upause;                     // "1" = tien ich dang tam dung (panel bo tick tai khoan)
    std::wstring pb, pbphase, pbfloor;       // pho ban dang chay
    int roses = -1;
    int lz = 0, lzb = -2, lzbmin = 0;
    int lzmax = -1, lzbrk = 0, lzresc = 0, lzgap = 0;  // thong ke luot Lien Tram dang chay (lzgap: don vi 0,1s)        // chuoi lien tram / buff lien tram (muc, phut con lai); lzb -2 = chua biet
};

struct Acc {
    std::wstring id;
    Config cfg;
    Status st;
    std::unique_ptr<Link> link;
    bool connected = false;
    bool utilSent = false;                   // da gui util_set cho lan vao game nay
    bool inXml = false;                      // co trong data\\accounts.xml (dang nhap duoc tu panel)
    bool hidden = false;                     // cua so game dang an
    RECT savedRect = {};
    DWORD renderAt = 0;                      // lan gui lai lenh render gan nhat
    bool active = false;                     // o tick tai khoan: true = client mo + cac chuc nang duoc chay
    bool manualOff = false;                  // nguoi dung bo tick khi client dang chay (khong tu tick lai khi noi lai)
    bool adopted = false;
    bool noPtsLogged = false;                // da bao "chua co toa do" (khong lap lai)
    std::wstring pbSkip[5];                  // ly do bo qua pho ban (het luot / thieu hoa / khong vao duoc)
    DWORD pbWaitAt[5] = {};                  // SWF bao "pb wait" (chua du dieu kien lien tram): tam bo ra khoi danh sach 60s
    DWORD pbStartAt = 0;                     // lan gui pb_start gan nhat
    bool hostClosing = false;                // vlcmhost bao dang dong theo y nguoi dung (khong tu mo lai)
    DWORD relaunchAt = 0;                    // hen mo lai client sau khi tat bat thuong
    std::vector<DWORD> relaunches;           // cac lan tu mo lai (de ghi log; khong gioi han so lan)
    DWORD gameDiscoAt = 0;                   // game bao mat ket noi (client van mo)                    // da xet "train dang chay san" cho lan ket noi nay
    DWORD pauseAt = 0, quitAt = 0;           // lan gui util_pause gan nhat / luc gui lenh quit
    std::wstring savedNv;                    // ten nhan vat da luu trong accounts.xml (hien khi client tat)
    DWORD pid = 0; ULONGLONG lastCpu = 0; DWORD lastCpuAt = 0; double cpu = -1;
    double cpuMs[3] = {}, cpuWall[3] = {};   // CPU theo tung che do (hien / an 1 / an 2)
    bool skillsAsked = false;
    bool everConnected = false;
    struct PendingCmd { std::wstring tag; DWORD at; };
    std::deque<PendingCmd> pending;          // tag + thoi diem gui, cho "> ..." theo thu tu (FIFO)
    std::vector<std::wstring> log;
    size_t curPt = 0;                        // chi so trong cac diem dang tick
    DWORD ptSince = 0, lastStatus = 0, lastStart = 0, restartAt = 0;
};

// ============================================================ data\accounts.xml (vlcmhost doc file nay)
// <accounts><acc><cong/><user/><pass/><sv/><kenh/><nv/><hidden/></acc>...</accounts>
// Mat khau luu dang "ENC:" + base64(DPAPI) — dung dinh dang AccountStore.h::DecodePass (chi may + user Windows nay giai duoc).

struct XmlAcc { std::wstring cong, user, pass, sv, kenh, nv, hidden; };

static std::wstring AccXmlPath() { return ExeDir() + L"\\data\\accounts.xml"; }
static std::wstring ReadUtf8File(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return L"";
    LARGE_INTEGER sz = {}; GetFileSizeEx(h, &sz);
    std::string d((size_t)sz.QuadPart, '\0'); DWORD n = 0;
    if (!d.empty()) ReadFile(h, &d[0], (DWORD)d.size(), &n, nullptr);
    CloseHandle(h);
    if (d.size() >= 3 && (unsigned char)d[0] == 0xEF && (unsigned char)d[1] == 0xBB) d.erase(0, 3);
    return FromUtf8(d);
}
static bool WriteUtf8File(const std::wstring& path, const std::wstring& text) {
    std::wstring tmp = path + L".tmp";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    std::string d = ToUtf8(text); DWORD n = 0;
    bool ok = WriteFile(h, d.data(), (DWORD)d.size(), &n, nullptr) && n == d.size();
    CloseHandle(h);
    return ok && MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
}
static std::wstring TagVal(const std::wstring& block, const std::wstring& tag) {
    size_t a = block.find(L"<" + tag + L">"); if (a == std::wstring::npos) return L"";
    a += tag.size() + 2;
    size_t b = block.find(L"</" + tag + L">", a); if (b == std::wstring::npos) return L"";
    return block.substr(a, b - a);
}
static std::vector<XmlAcc> LoadXmlAccounts() {
    std::vector<XmlAcc> v;
    std::wstring s = ReadUtf8File(AccXmlPath());
    size_t pos = 0;
    for (;;) {
        size_t a = s.find(L"<acc>", pos); if (a == std::wstring::npos) break;
        size_t b = s.find(L"</acc>", a); if (b == std::wstring::npos) break;
        std::wstring blk = s.substr(a, b - a);
        XmlAcc x{ TagVal(blk, L"cong"), TagVal(blk, L"user"), TagVal(blk, L"pass"), TagVal(blk, L"sv"),
                  TagVal(blk, L"kenh"), TagVal(blk, L"nv"), TagVal(blk, L"hidden") };
        if (!x.user.empty()) v.push_back(x);
        pos = b + 6;
    }
    return v;
}
static std::wstring XmlSafe(std::wstring v) { for (auto& c : v) if (c == L'<' || c == L'>' || c == L'\r' || c == L'\n') c = L' '; return v; }
static std::wstring SetTag(std::wstring blk, const std::wstring& tag, const std::wstring& val) {
    std::wstring o = L"<" + tag + L">", c = L"</" + tag + L">";
    size_t a = blk.find(o);
    if (a == std::wstring::npos) return blk + L"  " + o + XmlSafe(val) + c + L"\r\n  ";
    size_t b = blk.find(c, a + o.size());
    if (b == std::wstring::npos) return blk;
    return blk.substr(0, a + o.size()) + XmlSafe(val) + blk.substr(b);
}
/** them hoac sua mot tai khoan (theo user, khong phan biet hoa thuong), giu nguyen cac the khac trong <acc> */
static bool SaveXmlAccount(const XmlAcc& x) {
    std::wstring path = AccXmlPath();
    CreateDirectoryW((ExeDir() + L"\\data").c_str(), nullptr);
    std::wstring s = ReadUtf8File(path);
    static bool backed = false;
    if (!backed && !s.empty()) { CopyFileW(path.c_str(), (path + L".bak").c_str(), FALSE); backed = true; }
    if (s.find(L"</accounts>") == std::wstring::npos) s = L"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n<accounts>\r\n</accounts>\r\n";
    size_t pos = 0;
    for (;;) {
        size_t a = s.find(L"<acc>", pos); if (a == std::wstring::npos) break;
        size_t b = s.find(L"</acc>", a); if (b == std::wstring::npos) break;
        std::wstring blk = s.substr(a, b - a);
        if (_wcsicmp(TagVal(blk, L"user").c_str(), x.user.c_str()) == 0) {
            for (auto& kv : std::vector<std::pair<std::wstring, std::wstring>>{ { L"pass", x.pass }, { L"sv", x.sv }, { L"kenh", x.kenh }, { L"nv", x.nv } })
                blk = SetTag(blk, kv.first, kv.second);
            s = s.substr(0, a) + blk + s.substr(b);
            return WriteUtf8File(path, s);
        }
        pos = b + 6;
    }
    std::wstring blk = L"  <acc>\r\n    <cong>" + XmlSafe(x.cong) + L"</cong>\r\n    <user>" + XmlSafe(x.user) + L"</user>\r\n    <pass>" + XmlSafe(x.pass)
        + L"</pass>\r\n    <sv>" + XmlSafe(x.sv) + L"</sv>\r\n    <kenh>" + XmlSafe(x.kenh) + L"</kenh>\r\n    <nv>" + XmlSafe(x.nv)
        + L"</nv>\r\n    <hidden>0</hidden>\r\n  </acc>\r\n";
    size_t end = s.rfind(L"</accounts>");
    s = s.substr(0, end) + blk + s.substr(end);
    return WriteUtf8File(path, s);
}
static std::wstring EncPass(const std::wstring& plain) {
    if (plain.empty()) return L"";
    std::string u = ToUtf8(plain);
    DATA_BLOB in{ (DWORD)u.size(), (BYTE*)u.data() }, out{};
    if (!CryptProtectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return L"";
    DWORD n = 0;
    CryptBinaryToStringA(out.pbData, out.cbData, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &n);
    std::string b64(n, '\0');
    CryptBinaryToStringA(out.pbData, out.cbData, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, &b64[0], &n);
    LocalFree(out.pbData);
    b64.resize(strlen(b64.c_str()));
    return L"ENC:" + FromUtf8(b64);
}
static std::wstring DecPass(const std::wstring& raw) {
    if (raw.compare(0, 4, L"ENC:") != 0) return raw;          // chu thuong (file cu)
    std::string b64 = ToUtf8(raw.substr(4));
    DWORD n = 0;
    if (!CryptStringToBinaryA(b64.c_str(), (DWORD)b64.size(), CRYPT_STRING_BASE64, nullptr, &n, nullptr, nullptr)) return L"";
    std::vector<BYTE> blob(n);
    if (!CryptStringToBinaryA(b64.c_str(), (DWORD)b64.size(), CRYPT_STRING_BASE64, blob.data(), &n, nullptr, nullptr)) return L"";
    DATA_BLOB in{ n, blob.data() }, out{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return L"";
    std::string u((char*)out.pbData, out.cbData);
    LocalFree(out.pbData);
    return FromUtf8(u);
}
static int KenhNum(const std::wstring& k) { for (wchar_t c : k) if (c >= L'1' && c <= L'9') return c - L'0'; return 1; }

static std::vector<std::unique_ptr<Acc>> g_accs;     // thu tu = thu tu dong trong danh sach
static std::vector<std::wstring> g_argHosts;

static std::wstring DataDir() { std::wstring d = ExeDir() + L"\\data"; CreateDirectoryW(d.c_str(), nullptr); return d; }
static std::wstring IniPath(const std::wstring& acc) { return ExeDir() + L"\\data\\train_" + acc + L".ini"; }

static void IniPut(const std::wstring& f, const wchar_t* sec, const wchar_t* k, const std::wstring& v) { WritePrivateProfileStringW(sec, k, v.c_str(), f.c_str()); }
static std::wstring IniGet(const std::wstring& f, const wchar_t* sec, const wchar_t* k, const wchar_t* def) {
    std::vector<wchar_t> b(8192); GetPrivateProfileStringW(sec, k, def, b.data(), (DWORD)b.size(), f.c_str()); return b.data();
}
static std::wstring B(bool v) { return v ? L"1" : L"0"; }

static void SaveConfig(const Acc& a) {
    std::wstring f = IniPath(a.id);
    DataDir();
    if (GetFileAttributesW(f.c_str()) == INVALID_FILE_ATTRIBUTES) {       // file UTF-16 de giu tieng Viet
        HANDLE h = CreateFileW(f.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) { DWORD n; const unsigned char bom[2] = { 0xFF, 0xFE }; WriteFile(h, bom, 2, &n, nullptr); CloseHandle(h); }
    }
    const Config& c = a.cfg;
    WritePrivateProfileStringW(L"points", nullptr, nullptr, f.c_str());    // xoa het diem cu
    WritePrivateProfileStringW(L"rules", nullptr, nullptr, f.c_str());
    IniPut(f, L"train", L"skillmode2", std::to_wstring(c.skillMode));
    IniPut(f, L"train", L"skills", c.skillIds);
    IniPut(f, L"train", L"trainset", std::to_wstring(c.trainSet));
    for (int k = 0; k < 5; ++k) {
        std::wstring pre = L"set" + std::to_wstring(k + 1) + L"_";
        for (int g = 0; g < 4; ++g) IniPut(f, L"skills", (pre + L"g" + std::to_wstring(g + 1)).c_str(), c.sets[k].g[g]);
        IniPut(f, L"skills", (pre + L"heal").c_str(), c.sets[k].heal);
        IniPut(f, L"skills", (pre + L"buff").c_str(), c.sets[k].buff);
        IniPut(f, L"skills", (pre + L"supmp").c_str(), std::to_wstring(c.sets[k].supmp));
    }
    { std::wstring gs; for (int i = 0; i < 13; ++i) gs += c.gs[i] ? L"1" : L"0"; IniPut(f, L"game", L"gs", gs); }
    IniPut(f, L"game", L"hpfx", B(c.hpfx));
    IniPut(f, L"game", L"closeui", B(c.closeUi));
    for (int i = 0; i < NPB; ++i) {
        IniPut(f, L"pb", (L"range" + std::to_wstring(i)).c_str(), std::to_wstring(c.pbRange[i]));
        IniPut(f, L"pb", (L"set" + std::to_wstring(i)).c_str(), std::to_wstring(c.pbSet[i]));
        IniPut(f, L"pb", (L"npce" + std::to_wstring(i)).c_str(), std::to_wstring(c.npcEnter[i]));
        IniPut(f, L"pb", (L"npct" + std::to_wstring(i)).c_str(), std::to_wstring(c.npcTotal[i]));
        IniPut(f, L"pb", (L"rev" + std::to_wstring(i)).c_str(), std::to_wstring(c.pbRev[i]));
        IniPut(f, L"pb", (L"done" + std::to_wstring(i)).c_str(), std::to_wstring(c.pbDone[i]));
    }
    IniPut(f, L"pb", L"tqminr", std::to_wstring(c.tqMinR));
    IniPut(f, L"pb", L"dtjump", B(c.dtJump));
    IniPut(f, L"pb", L"dtjmax", std::to_wstring(c.dtJmax));
    IniPut(f, L"pb", L"ptjump", B(c.ptJump));
    IniPut(f, L"pb", L"ptbow", B(c.ptBow));
    IniPut(f, L"pb", L"mcfarm", std::to_wstring(c.mcFarm));
    IniPut(f, L"pb", L"mcfarmmin", std::to_wstring(c.mcFarmMin));
    IniPut(f, L"pb", L"mcfarmby", std::to_wstring(c.mcFarmBy));
    IniPut(f, L"pb", L"mcfarmlz", std::to_wstring(c.mcFarmLz));
    IniPut(f, L"pb", L"mcskip", B(c.mcSkip));
    for (int i = 0; i < NPB; ++i) {
        std::wstring n = std::to_wstring(i);
        IniPut(f, L"pb", (L"timeon" + n).c_str(), B(c.pbTimeOn[i]));
        IniPut(f, L"pb", (L"time" + n).c_str(), std::to_wstring(c.pbH1[i]) + L":" + std::to_wstring(c.pbM1[i]) + L"-" + std::to_wstring(c.pbH2[i]) + L":" + std::to_wstring(c.pbM2[i]));
        IniPut(f, L"pb", (L"lzon" + n).c_str(), B(c.pbLzOn[i]));
        IniPut(f, L"pb", (L"lz" + n).c_str(), std::to_wstring(c.pbLzVal[i]) + L":" + std::to_wstring(c.pbLzMin[i]));
        IniPut(f, L"pb", (L"sfon" + n).c_str(), B(c.pbSfOn[i]));
        IniPut(f, L"pb", (L"sf" + n).c_str(), std::to_wstring(c.pbSf[i]));
        IniPut(f, L"pb", (L"nmon" + n).c_str(), B(c.pbNmOn[i]));
        IniPut(f, L"pb", (L"nm" + n).c_str(), std::to_wstring(c.pbNm[i]));
    }
    IniPut(f, L"pb", L"ltstopon", B(c.ltStopOn)); IniPut(f, L"pb", L"lty", std::to_wstring(c.ltY)); IniPut(f, L"pb", L"ltcont", B(c.ltCont));
    IniPut(f, L"pb", L"ltlureon", B(c.ltLureOn)); IniPut(f, L"pb", L"ltx", std::to_wstring(c.ltX)); IniPut(f, L"pb", L"ltkpp", std::to_wstring(c.ltKpp));
    IniPut(f, L"pb", L"tqafk", B(c.tqAfk)); IniPut(f, L"pb", L"tqbosscount", B(c.tqBossCount));
    IniPut(f, L"pb", L"ltpick", std::to_wstring(c.ltPick)); IniPut(f, L"pb", L"ltskipboss", B(c.ltSkipBoss));
    IniPut(f, L"pb", L"date", c.pbDate);
    IniPut(f, L"game", L"hidelevel", std::to_wstring(c.hideLevel));
    IniPut(f, L"train", L"keys", c.keys);
    IniPut(f, L"train", L"pickmode", std::to_wstring(c.pickMode));
    IniPut(f, L"train", L"picklist", c.pickList);
    IniPut(f, L"train", L"hpkey", c.hpkey);
    IniPut(f, L"train", L"hp", std::to_wstring(c.hp));
    IniPut(f, L"train", L"types", std::wstring(c.tNormal ? L"1" : L"") + (c.tElite ? L"2" : L"") + (c.tBoss ? L"3" : L""));
    IniPut(f, L"train", L"rest", B(c.restOn));
    IniPut(f, L"train", L"restdeaths", std::to_wstring(c.restDeaths));
    IniPut(f, L"train", L"restmin", std::to_wstring(c.restMin));
    IniPut(f, L"train", L"switchtime", B(c.swTimeOn));
    IniPut(f, L"train", L"switchmin", std::to_wstring(c.swMin));
    IniPut(f, L"train", L"switchdeath", B(c.swDeath));
    IniPut(f, L"train", L"enabled", B(c.enabled));
    IniPut(f, L"util", L"potmode", std::to_wstring(c.potMode));
    IniPut(f, L"util", L"mpkey", c.mpkey);
    IniPut(f, L"util", L"mp", std::to_wstring(c.mp));
    IniPut(f, L"util", L"drugs", c.drugIds);
    IniPut(f, L"util", L"repair", B(c.repairOn));
    IniPut(f, L"util", L"repairtype", std::to_wstring(c.repairType));
    IniPut(f, L"util", L"bh", B(c.bhOn));
    IniPut(f, L"util", L"freee", std::to_wstring(c.freeE));
    IniPut(f, L"util", L"freei", std::to_wstring(c.freeI));
    IniPut(f, L"util", L"floorq", B(c.floorQ));
    IniPut(f, L"util", L"floors", std::to_wstring(c.floorS));
    IniPut(f, L"util", L"protect", c.protect);
    IniPut(f, L"util", L"tele", B(c.tele));
    IniPut(f, L"util", L"vllauto", B(c.vllAuto));
    IniPut(f, L"rules", L"count", std::to_wstring(c.rules.size()));
    for (size_t i = 0; i < c.rules.size(); ++i) IniPut(f, L"rules", (L"r" + std::to_wstring(i)).c_str(), RuleLine(c.rules[i]));
    IniPut(f, L"points", L"count", std::to_wstring(c.pts.size()));
    for (size_t i = 0; i < c.pts.size(); ++i) {
        const TrainPoint& p = c.pts[i];
        IniPut(f, L"points", (L"p" + std::to_wstring(i)).c_str(),
               std::to_wstring(p.map) + L"," + std::to_wstring(p.x) + L"," + std::to_wstring(p.y) + L"," +
               std::to_wstring(p.r) + L"," + (p.on ? L"1" : L"0") + L"," + p.name);
    }
}

static void LoadConfig(Acc& a) {
    std::wstring f = IniPath(a.id);
    if (GetFileAttributesW(f.c_str()) == INVALID_FILE_ATTRIBUTES) return;
    Config& c = a.cfg;
    c.skillIds = IniGet(f, L"train", L"skills", L"");
    std::wstring sm2 = IniGet(f, L"train", L"skillmode2", L"");
    c.trainSet = (std::min)(4, (std::max)(0, ToInt(IniGet(f, L"train", L"trainset", L"0"))));
    for (int k = 0; k < 5; ++k) {
        std::wstring pre = L"set" + std::to_wstring(k + 1) + L"_";
        for (int g = 0; g < 4; ++g) c.sets[k].g[g] = IniGet(f, L"skills", (pre + L"g" + std::to_wstring(g + 1)).c_str(), L"");
        c.sets[k].heal = IniGet(f, L"skills", (pre + L"heal").c_str(), L"");
        c.sets[k].buff = IniGet(f, L"skills", (pre + L"buff").c_str(), L"");
        c.sets[k].supmp = ToInt(IniGet(f, L"skills", (pre + L"supmp").c_str(), L"30"), 30);
    }
    if (sm2.empty()) {                       // chuyen tu cai dat cu: phim -> phim; chon skill -> nhom 1 (theo dau ID)
        int old = ToInt(IniGet(f, L"train", L"skillmode", L"0"));
        c.skillMode = old == 2 ? 1 : 0;
        if (old == 1 && !NumList(c.skillIds).empty()) {
            for (auto& id : [&]() { std::vector<std::wstring> v; std::wstring l = NumList(c.skillIds); size_t i = 0;
                                     while (i < l.size()) { size_t e = l.find(L',', i); if (e == std::wstring::npos) e = l.size(); v.push_back(l.substr(i, e - i)); i = e + 1; }
                                     return v; }()) {
                int pfx = ToInt(id) / 1000, g = pfx == 51 ? 0 : pfx == 52 ? 1 : pfx == 53 ? 2 : 3;
                std::wstring& dst = c.sets[0].g[g]; dst += (dst.empty() ? L"" : L",") + id;
            }
        }
    } else c.skillMode = ToInt(sm2) == 1 ? 1 : 0;
    { std::wstring gs = IniGet(f, L"game", L"gs", L""); for (int i = 0; i < 13 && i < (int)gs.size(); ++i) c.gs[i] = gs[i] == L'1'; }
    c.hpfx = IniGet(f, L"game", L"hpfx", L"0") == L"1";
    c.closeUi = IniGet(f, L"game", L"closeui", L"1") == L"1";
    for (int i = 0; i < NPB; ++i) {
        c.pbRange[i] = (std::min)(999, (std::max)(1, ToInt(IniGet(f, L"pb", (L"range" + std::to_wstring(i)).c_str(), L"99"), 99)));
        c.pbSet[i] = (std::min)(4, (std::max)(-1, ToInt(IniGet(f, L"pb", (L"set" + std::to_wstring(i)).c_str(), L"-1"), -1)));
        c.npcEnter[i] = ToInt(IniGet(f, L"pb", (L"npce" + std::to_wstring(i)).c_str(), L"-1"), -1);
        c.npcTotal[i] = ToInt(IniGet(f, L"pb", (L"npct" + std::to_wstring(i)).c_str(), L"-1"), -1);
        c.pbRev[i] = (std::max)(0, ToInt(IniGet(f, L"pb", (L"rev" + std::to_wstring(i)).c_str(), L"2"), 2));
        c.pbDone[i] = (std::max)(0, ToInt(IniGet(f, L"pb", (L"done" + std::to_wstring(i)).c_str(), L"0"), 0));
    }
    c.tqMinR = (std::max)(0, ToInt(IniGet(f, L"pb", L"tqminr", L"15"), 15));
    c.dtJump = IniGet(f, L"pb", L"dtjump", L"1") == L"1";
    c.dtJmax = ToInt(IniGet(f, L"pb", L"dtjmax", L"0"), 0) == 1 ? 1 : 0;
    c.ptJump = IniGet(f, L"pb", L"ptjump", L"1") == L"1";
    c.ptBow = IniGet(f, L"pb", L"ptbow", L"1") == L"1";
    c.mcFarm = (std::min)(16, (std::max)(0, ToInt(IniGet(f, L"pb", L"mcfarm", L"0"), 0)));
    c.mcFarmMin = (std::max)(1, ToInt(IniGet(f, L"pb", L"mcfarmmin", L"10"), 10));
    c.mcFarmBy = ToInt(IniGet(f, L"pb", L"mcfarmby", L"0"), 0) == 1 ? 1 : 0;
    c.mcFarmLz = (std::max)(1, ToInt(IniGet(f, L"pb", L"mcfarmlz", L"300"), 300));
    c.mcSkip = IniGet(f, L"pb", L"mcskip", L"0") == L"1";
    for (int i = 0; i < NPB; ++i) {
        std::wstring n = std::to_wstring(i);
        c.pbTimeOn[i] = IniGet(f, L"pb", (L"timeon" + n).c_str(), L"0") == L"1";
        std::wstring t = IniGet(f, L"pb", (L"time" + n).c_str(), L"");
        int h1, m1, h2, m2;
        if (swscanf(t.c_str(), L"%d:%d-%d:%d", &h1, &m1, &h2, &m2) == 4) { c.pbH1[i] = h1; c.pbM1[i] = m1; c.pbH2[i] = h2; c.pbM2[i] = m2; }
        c.pbLzOn[i] = IniGet(f, L"pb", (L"lzon" + n).c_str(), L"0") == L"1";
        std::wstring z = IniGet(f, L"pb", (L"lz" + n).c_str(), L"");
        int zv, zm;
        if (swscanf(z.c_str(), L"%d:%d", &zv, &zm) == 2) { c.pbLzVal[i] = zv; c.pbLzMin[i] = zm; }
        c.pbSfOn[i] = IniGet(f, L"pb", (L"sfon" + n).c_str(), L"0") == L"1";
        c.pbSf[i] = (std::max)(0, ToInt(IniGet(f, L"pb", (L"sf" + n).c_str(), std::to_wstring(c.pbSf[i]).c_str()), c.pbSf[i]));
        c.pbNmOn[i] = IniGet(f, L"pb", (L"nmon" + n).c_str(), L"0") == L"1";
        c.pbNm[i] = (std::max)(1, ToInt(IniGet(f, L"pb", (L"nm" + n).c_str(), L"3"), 3));
    }
    c.ltStopOn = IniGet(f, L"pb", L"ltstopon", L"1") == L"1"; c.ltY = (std::max)(1, ToInt(IniGet(f, L"pb", L"lty", L"500"), 500));
    c.ltCont = IniGet(f, L"pb", L"ltcont", L"1") == L"1";
    c.ltLureOn = IniGet(f, L"pb", L"ltlureon", L"1") == L"1"; c.ltX = (std::max)(0, ToInt(IniGet(f, L"pb", L"ltx", L"100"), 100));
    c.ltKpp = (std::min)(5, (std::max)(1, ToInt(IniGet(f, L"pb", L"ltkpp", L"2"), 2)));
    c.tqAfk = IniGet(f, L"pb", L"tqafk", L"1") == L"1"; c.tqBossCount = IniGet(f, L"pb", L"tqbosscount", L"1") == L"1";
    c.ltPick = ToInt(IniGet(f, L"pb", L"ltpick", L"0"), 0) == 1 ? 1 : 0; c.ltSkipBoss = IniGet(f, L"pb", L"ltskipboss", L"0") == L"1";
    c.pbDate = IniGet(f, L"pb", L"date", L"");
    c.hideLevel = ToInt(IniGet(f, L"game", L"hidelevel", L"1"), 1) == 2 ? 2 : 1;
    c.keys = IniGet(f, L"train", L"keys", L"1,2,3");
    c.pickMode = ToInt(IniGet(f, L"train", L"pickmode", L"0"));
    c.pickList = IniGet(f, L"train", L"picklist", DEFAULT_PICKLIST);
    c.hpkey = IniGet(f, L"train", L"hpkey", L"");
    c.hp = ToInt(IniGet(f, L"train", L"hp", L"40"), 40);
    std::wstring t = IniGet(f, L"train", L"types", L"12");
    c.tNormal = t.find(L'1') != std::wstring::npos; c.tElite = t.find(L'2') != std::wstring::npos; c.tBoss = t.find(L'3') != std::wstring::npos;
    c.restOn = IniGet(f, L"train", L"rest", L"0") == L"1";
    c.restDeaths = ToInt(IniGet(f, L"train", L"restdeaths", L"10"), 10);
    c.restMin = ToInt(IniGet(f, L"train", L"restmin", L"5"), 5);
    c.swTimeOn = IniGet(f, L"train", L"switchtime", L"0") == L"1";
    c.swMin = ToInt(IniGet(f, L"train", L"switchmin", L"30"), 30);
    c.swDeath = IniGet(f, L"train", L"switchdeath", L"0") == L"1";
    c.enabled = IniGet(f, L"train", L"enabled", L"0") == L"1";
    c.potMode = ToInt(IniGet(f, L"util", L"potmode", L"0"));
    c.mpkey = IniGet(f, L"util", L"mpkey", L"");
    c.mp = ToInt(IniGet(f, L"util", L"mp", L"20"), 20);
    c.drugIds = IniGet(f, L"util", L"drugs", Config().drugIds.c_str());
    c.repairOn = IniGet(f, L"util", L"repair", L"0") == L"1";
    c.repairType = ToInt(IniGet(f, L"util", L"repairtype", L"1"), 1) == 2 ? 2 : 1;
    c.bhOn = IniGet(f, L"util", L"bh", L"0") == L"1";
    c.freeE = ToInt(IniGet(f, L"util", L"freee", L"5"), 5);
    c.freeI = ToInt(IniGet(f, L"util", L"freei", L"5"), 5);
    c.floorQ = IniGet(f, L"util", L"floorq", L"1") == L"1";
    c.floorS = ToInt(IniGet(f, L"util", L"floors", L"1"), 1);
    c.protect = IniGet(f, L"util", L"protect", Config().protect.c_str());
    c.tele = IniGet(f, L"util", L"tele", L"1") == L"1";
    c.vllAuto = IniGet(f, L"util", L"vllauto", L"0") == L"1";
    c.rules.clear();
    int nr = ToInt(IniGet(f, L"rules", L"count", L"0"));
    for (int i = 0; i < nr; ++i) {
        Rule r;
        if (ParseRuleLine(IniGet(f, L"rules", (L"r" + std::to_wstring(i)).c_str(), L""), r)) c.rules.push_back(r);
    }
    c.pts.clear();
    int n = ToInt(IniGet(f, L"points", L"count", L"0"));
    for (int i = 0; i < n; ++i) {
        std::wstring v = IniGet(f, L"points", (L"p" + std::to_wstring(i)).c_str(), L"");
        std::vector<std::wstring> parts;
        size_t s = 0;
        for (int k = 0; k < 5; ++k) {                              // 5 truong so, phan con lai la ten map
            size_t e = v.find(L',', s);
            if (e == std::wstring::npos) break;
            parts.push_back(v.substr(s, e - s)); s = e + 1;
        }
        if (parts.size() < 5) continue;
        TrainPoint p;
        p.map = ToInt(parts[0]); p.x = ToInt(parts[1]); p.y = ToInt(parts[2]); p.r = ToInt(parts[3], 5); p.on = parts[4] == L"1";
        p.name = v.substr(s);
        c.pts.push_back(p);
    }
}

// ============================================================ giao dien

enum {
    IDC_ACCLIST = 100, IDC_BTN_STARTALL, IDC_BTN_STOPALL, IDC_CONNINFO,
    IDC_LBL_CTRL, IDC_CHK_TRAIN, IDC_TRAIN_STATE, IDC_BTN_GEAR,
    IDC_LBL_VIP, IDC_LBL_STAT1, IDC_LBL_STAT2, IDC_LBL_STAT3, IDC_LBL_BHSTATE, IDC_BTN_BHSTOP, IDC_BTN_BHRESET, IDC_BTN_STATRESET,
    IDC_TAB_MAIN, IDC_LOG, IDC_TAB_TRAIN, IDC_TAB_UTIL,
    // TRAIN
    IDC_PTLIST, IDC_BTN_ADDPT, IDC_BTN_DELPT, IDC_LBL_R, IDC_EDIT_R, IDC_BTN_SETR,
    IDC_LBL_TYPES, IDC_CHK_T1, IDC_CHK_T2, IDC_CHK_T3,
    IDC_CHK_REST, IDC_EDIT_RESTN, IDC_LBL_REST2, IDC_EDIT_RESTMIN, IDC_LBL_REST3,
    IDC_BTN_APPLYALL, IDC_LBL_NOTE,
    IDC_CHK_SWTIME, IDC_EDIT_SWMIN, IDC_LBL_SWMIN, IDC_CHK_SWDEATH, IDC_LBL_SWNOTE,
    // TIEN ICH - ky nang
    IDC_LBL_SKMODE, IDC_CMB_SKMODE, IDC_LBL_KEYS, IDC_EDIT_KEYS, IDC_SKLIST, IDC_BTN_GETSKILLS, IDC_LBL_SKNOTE,
    IDC_LBL_SKSET, IDC_CMB_SKSET, IDC_TAB_SK, IDC_LBL_SKADD, IDC_CMB_SKADD, IDC_BTN_SKADD, IDC_BTN_SKUP, IDC_BTN_SKDOWN, IDC_BTN_SKDEL,
    IDC_SUPLIST, IDC_CMB_SUPTYPE, IDC_CMB_SUPSKILL, IDC_LBL_SUPPCT, IDC_EDIT_SUPPCT, IDC_BTN_SUPADD, IDC_BTN_SUPDEL, IDC_LBL_SUPMP, IDC_EDIT_SUPMP, IDC_LBL_SUPNOTE,
    IDC_LBL_TRAINSET, IDC_CMB_TRAINSET,
    // thuoc
    IDC_LBL_POTMODE, IDC_CMB_POTMODE, IDC_LBL_HPKEY, IDC_EDIT_HPKEY, IDC_LBL_MPKEY, IDC_EDIT_MPKEY,
    IDC_LBL_HP, IDC_EDIT_HP, IDC_LBL_MP, IDC_EDIT_MP, IDC_DRUGLIST, IDC_LBL_POTNOTE,
    // nhat do
    IDC_LBL_PKMODE, IDC_CMB_PKMODE, IDC_EDIT_PKLIST, IDC_BTN_GETBAG, IDC_BAGLIST, IDC_BTN_ADDBAG, IDC_BTN_PKDEFAULT, IDC_LBL_PKNOTE,
    // sua do
    IDC_CHK_REPAIR, IDC_LBL_RTYPE, IDC_CMB_RTYPE, IDC_LBL_REPNOTE,
    // ban huy
    IDC_CHK_BH, IDC_RULELIST, IDC_LBL_R_ACT, IDC_CMB_R_ACT, IDC_CMB_R_KIND, IDC_LBL_R_Q, IDC_CMB_R_Q,
    IDC_LBL_R_S, IDC_EDIT_R_S, IDC_LBL_R_G, IDC_EDIT_R_G, IDC_LBL_R_P, IDC_EDIT_R_P, IDC_LBL_R_POS, IDC_EDIT_R_POS,
    IDC_LBL_R_LOCK, IDC_CMB_R_LOCK, IDC_LBL_R_NAMES, IDC_EDIT_R_NAMES, IDC_BTN_R_ADD, IDC_BTN_R_DEL,
    IDC_LBL_FREEE, IDC_EDIT_FREEE, IDC_LBL_FREEI, IDC_EDIT_FREEI, IDC_LBL_BHNOTE,
    IDC_RULELIST2, IDC_CHK_BH2, IDC_CMB_R_LV, IDC_LBL_R_TITLE, IDC_LBL_BHNOTE2,
    // cai dat game
    IDC_LBL_GSNOTE, IDC_CHK_HPFX, IDC_LBL_HIDELV, IDC_CMB_HIDELV, IDC_CHK_CLOSEUI,
    // pho ban (TRAIN > PHO BAN)
    IDC_LBL_TQMINR, IDC_EDIT_TQMINR, IDC_CHK_DTJUMP, IDC_LBL_PBNOTE,
    IDC_CHK_PTJUMP, IDC_CHK_PTBOW, IDC_LBL_MCFARM, IDC_CMB_MCFARM, IDC_LBL_MCMIN, IDC_EDIT_MCMIN,
    IDC_LBL_DTJMAX, IDC_CMB_DTJMAX,
    IDC_LBL_MCBY, IDC_CMB_MCBY, IDC_LBL_MCLZ, IDC_EDIT_MCLZ, IDC_CHK_MCSKIP,
    // hieu nang (tab chinh)
    IDC_PERFLIST, IDC_BTN_PERFRESET, IDC_LBL_PERFNOTE,
    // an toan
    IDC_CHK_FLOORQ, IDC_LBL_FLOORS, IDC_EDIT_FLOORS, IDC_LBL_PROTECT, IDC_EDIT_PROTECT, IDC_BTN_DUMP, IDC_LBL_SAFENOTE,
    // di chuyen
    IDC_CHK_TELE, IDC_CHK_VLL, IDC_LBL_MOVENOTE,
    // TAI KHOAN
    IDC_LBL_A_TITLE, IDC_LBL_A_USER, IDC_EDIT_A_USER, IDC_LBL_A_PASS, IDC_EDIT_A_PASS, IDC_BTN_A_EYE,
    IDC_LBL_A_SV, IDC_EDIT_A_SV, IDC_LBL_A_KENH, IDC_CMB_A_KENH, IDC_LBL_A_NV, IDC_EDIT_A_NV,
    IDC_BTN_A_SAVE, IDC_BTN_A_LOGIN, IDC_BTN_A_CANCEL, IDC_LBL_A_NOTE,
    IDM_LOGIN = 900, IDM_NEWWIN, IDM_HIDE, IDM_SHOW, IDM_HIDEALL, IDM_SHOWALL, IDM_QUIT, IDM_QUITALL,
    IDC_CHK_GS0 = 1000,          // 13 o cai dat game
    IDC_CHK_PB0 = 1040,          // 5 o pho ban (cot DIEU KHIEN)
    IDC_PB_ST0 = 1046,
    IDC_BTN_PBG0 = 1052,
    IDC_LBL_PBNAME0 = 1058, IDC_LBL_PBRUNS0 = 1064, IDC_EDIT_PBRUNS0 = 1070, IDC_LBL_PBREV0 = 1076, IDC_EDIT_PBREV0 = 1082, IDC_LBL_PBDONE0 = 1088,
    IDC_CHK_SECT0 = 1100,        // 4 mon phai
    IDC_CHK_SLOT0 = 1120,        // 12 loai trang bi
};

static HWND g_hwnd, g_acc, g_tabMain, g_log, g_tabTrain, g_tabUtil, g_pts, g_trainState, g_tip, g_rules, g_rules2, g_drugs;
static HWND g_tabSk, g_skOrder, g_supList, g_perf;
static std::vector<HWND> g_pageSkGroup, g_pageSkSup, g_pagePerf;   // trong trang KY NANG: 4 tab nhom / tab ho tro
static int g_skSet = 0;                     // nhom ky nang dang sua
static int g_dragRow = -1;
static HFONT g_font, g_fontBold;
static HBRUSH g_brLog, g_brBlue, g_brYellow, g_brGray, g_brGreen;
static int g_sel = -1;
static bool g_loading = false;
static std::vector<HWND> g_pageLog;
static std::vector<HWND> g_pgTrain[4];     // TOA DO, CAI DAT KHAC, CHUYEN BAI, PHO BAN
static const wchar_t* PB_KEY[NPB] = { L"lt", L"tq", L"dt", L"pt", L"mc" };
static const wchar_t* PB_NAME[NPB] = { L"Liên Trảm", L"Thiên Quan", L"Doanh Trại", L"Phu Tử Trận", L"Mê Cung Trận" };
static const wchar_t* PB_FILE[NPB] = { L"LienTram.xml", L"ThienQuan.xml", L"DoanhTrai.xml", L"PhuTu.xml", L"MeCung.xml" };
static HWND g_pbState[NPB];
// khung cai dat rieng tung pho ban (hien ben phai danh sach khi bam nut "..." cua dong pho ban)
static const int IDC_PBP_FRAME = 1190, IDC_PBP_TITLE = 1191, IDC_PBP_CLOSE = 1192;
static int PBX(int i, int k) { return 1200 + i * 40 + k; }
enum { PX_TIMEON, PX_H1, PX_LH1, PX_M1, PX_LM1, PX_H2, PX_LH2, PX_M2, PX_LM2,
       PX_LZON, PX_LZVAL, PX_LLZ, PX_LZMIN, PX_LLZM,
       PX_SFON, PX_SF, PX_NMON, PX_NM, PX_LNM,
       PX_LTSTOP = 20, PX_LTY, PX_LTCONT, PX_LTLURE, PX_LTX, PX_LKPP, PX_LTKPP, PX_LKPP2, PX_LPICK, PX_LTPICK, PX_LTSKIP,
       PX_NOTE = 32, PX_INFO = 33, PX_TQAFK = 34, PX_TQBC = 35, PX_SET = 36, PX_LSET = 37 };
static int g_pbPane = -1;                   // pho ban dang mo khung cai dat (-1 = khong)
static RECT g_pbRect = {};                  // vung khung (nen trang ve trong WM_PAINT)
static std::vector<HWND> g_pgPb[NPB], g_pgPbCommon;
static const int NUTIL = 9;
static std::vector<HWND> g_pgUtil[NUTIL];  // KY NANG, THUOC, NHAT DO, SUA DO, BAN, HUY, AN TOAN, DI CHUYEN, CAI DAT GAME
static std::vector<HWND> g_pageAcc;         // TAI KHOAN
static bool g_newAcc = false;               // khung tai khoan dang o che do "cua so moi"
static bool g_showPass = false;

static HWND Ctl(const wchar_t* cls, const wchar_t* text, DWORD style, int id, std::vector<HWND>* page = nullptr, DWORD ex = 0) {
    HWND h = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, g_hwnd, (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(h, WM_SETFONT, (WPARAM)g_font, TRUE);
    if (page) page->push_back(h);
    return h;
}
static HWND Item(int id) { return GetDlgItem(g_hwnd, id); }
static void Move(int id, int x, int y, int w, int h) { MoveWindow(Item(id), x, y, w, h, TRUE); }
static std::wstring GetText(int id) {
    int n = GetWindowTextLengthW(Item(id));
    std::wstring s((size_t)n, L'\0');
    if (n) GetWindowTextW(Item(id), &s[0], n + 1);
    return s;
}
static void SetText(int id, const std::wstring& s) { SetWindowTextW(Item(id), s.c_str()); }
static bool Checked(int id) { return SendMessageW(Item(id), BM_GETCHECK, 0, 0) == BST_CHECKED; }
static void Check(int id, bool on) { SendMessageW(Item(id), BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0); }
static int CurSel(int id) { return (std::max)(0, (int)SendMessageW(Item(id), CB_GETCURSEL, 0, 0)); }
static void SetSel(int id, int i) { SendMessageW(Item(id), CB_SETCURSEL, i, 0); }
static void AddItems(HWND cmb, std::initializer_list<const wchar_t*> items) { for (auto s : items) SendMessageW(cmb, CB_ADDSTRING, 0, (LPARAM)s); }

static Acc* Sel() { return g_sel >= 0 && g_sel < (int)g_accs.size() ? g_accs[g_sel].get() : nullptr; }
static Acc* Find(const std::wstring& id) { for (auto& a : g_accs) if (a->id == id) return a.get(); return nullptr; }
static int IndexOf(const Acc* a) { for (size_t i = 0; i < g_accs.size(); ++i) if (g_accs[i].get() == a) return (int)i; return -1; }

static bool IsOnPage(HWND c) {
    for (auto& pg : g_pgTrain) if (std::find(pg.begin(), pg.end(), c) != pg.end()) return true;
    for (auto& pg : g_pgUtil) if (std::find(pg.begin(), pg.end(), c) != pg.end()) return true;
    for (auto* pg : { &g_pageSkGroup, &g_pageSkSup, &g_pagePerf }) if (std::find(pg->begin(), pg->end(), c) != pg->end()) return true;
    if (std::find(g_pageAcc.begin(), g_pageAcc.end(), c) != g_pageAcc.end()) return true;
    for (auto& pg : g_pgPb) if (std::find(pg.begin(), pg.end(), c) != pg.end()) return true;
    if (std::find(g_pgPbCommon.begin(), g_pgPbCommon.end(), c) != g_pgPbCommon.end()) return true;
    return false;
}
static void LoadAccForm();
static void ShowPage(std::vector<HWND>& page, bool on) { for (HWND h : page) ShowWindow(h, on ? SW_SHOW : SW_HIDE); }
static void UpdatePages() {
    int m = TabCtrl_GetCurSel(g_tabMain);
    int st = TabCtrl_GetCurSel(g_tabTrain), su = TabCtrl_GetCurSel(g_tabUtil), sk = TabCtrl_GetCurSel(g_tabSk);
    // an het truoc, hien sau: mot so control dung chung cho 2 trang (BAN va HUY)
    ShowPage(g_pageLog, false); ShowPage(g_pageAcc, false); ShowPage(g_pagePerf, false);
    for (int i = 0; i < 4; ++i) ShowPage(g_pgTrain[i], false);
    for (int i = 0; i < NUTIL; ++i) ShowPage(g_pgUtil[i], false);
    ShowPage(g_pageSkGroup, false); ShowPage(g_pageSkSup, false);
    ShowPage(g_pgPbCommon, g_pbPane >= 0);
    InvalidateRect(g_hwnd, &g_pbRect, TRUE);
    for (int i = 0; i < NPB; ++i) ShowPage(g_pgPb[i], i == g_pbPane);
    if (g_pbPane >= 0) {                     // khung cai dat pho ban che cho cac tab ben phai
        ShowWindow(g_tabMain, SW_HIDE); ShowWindow(g_tabTrain, SW_HIDE); ShowWindow(g_tabUtil, SW_HIDE);
        std::wstring t = std::wstring(L"PHÓ BẢN ") + PB_NAME[g_pbPane];
        CharUpperBuffW(&t[0], (DWORD)t.size());
        SetWindowTextW(GetDlgItem(g_hwnd, IDC_PBP_TITLE), t.c_str());
        return;
    }
    ShowWindow(g_tabMain, SW_SHOW);
    ShowWindow(g_tabTrain, m == 1 ? SW_SHOW : SW_HIDE);
    ShowWindow(g_tabUtil, m == 2 ? SW_SHOW : SW_HIDE);
    if (m == 0) ShowPage(g_pageLog, true);
    if (m == 1) ShowPage(g_pgTrain[st], true);
    if (m == 2) {
        ShowPage(g_pgUtil[su], true);
        if (su == 0) { ShowPage(g_pageSkGroup, sk < 4); ShowPage(g_pageSkSup, sk == 4); }
    }
    if (m == 3) { ShowPage(g_pageAcc, true); if (!g_newAcc) ShowWindow(Item(IDC_BTN_A_CANCEL), SW_HIDE); }
    if (m == 4) ShowPage(g_pagePerf, true);
}

// ------------------------------------------------------------ log

static void AppendFile(const std::wstring& path, const std::wstring& line) {
    HANDLE h = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER sz = {}; GetFileSizeEx(h, &sz);
    std::string d = (sz.QuadPart == 0 ? std::string("\xEF\xBB\xBF") : std::string()) + ToUtf8(line) + "\r\n";
    DWORD n; WriteFile(h, d.data(), (DWORD)d.size(), &n, nullptr);
    CloseHandle(h);
}
static std::wstring Today() {
    SYSTEMTIME t; GetLocalTime(&t);
    wchar_t b[32]; swprintf(b, 32, L"%04d-%02d-%02d %02d:%02d:%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    return b;
}

static void AddLog(Acc* a, const std::wstring& text) {
    std::wstring line = L"[" + Now() + L"] " + text;
    a->log.push_back(line);
    if (a->log.size() > 500) a->log.erase(a->log.begin(), a->log.begin() + 100);
    if (a == Sel()) {
        std::wstring add = line + L"\r\n";
        int len = GetWindowTextLengthW(g_log);
        if (len > 60000) { SetWindowTextW(g_log, L""); len = 0; }
        SendMessageW(g_log, EM_SETSEL, len, len);
        SendMessageW(g_log, EM_REPLACESEL, FALSE, (LPARAM)add.c_str());
    }
}
static void RefreshLog() {
    std::wstring all;
    if (Acc* a = Sel()) for (auto& l : a->log) all += l + L"\r\n";
    SetWindowTextW(g_log, all.c_str());
    int len = GetWindowTextLengthW(g_log);
    SendMessageW(g_log, EM_SETSEL, len, len);
    SendMessageW(g_log, EM_SCROLLCARET, 0, 0);
}

// ------------------------------------------------------------ danh sach acc + dieu khien

static const wchar_t* PB_NAMEC[5] = { L"Liên Trảm", L"Thiên Quan", L"Doanh Trại", L"Phu Tử Trận", L"Mê Cung Trận" };
static int PbIndexC(const std::wstring& k) { return k == L"lt" ? 0 : k == L"tq" ? 1 : k == L"dt" ? 2 : k == L"pt" ? 3 : k == L"mc" ? 4 : -1; }
static std::wstring StateText(const Acc& a) {
    if (!a.connected) return a.everConnected ? L"Mất kết nối" : L"OFFLINE";
    if (!a.st.inGame) return L"Chưa vào game";
    const std::wstring& s = a.st.state;
    if (s == L"idle") return L"Đứng yên";
    if (s == L"going") return L"Đang đi";
    if (s == L"fighting") return L"Đánh quái";
    if (s == L"dead") return L"Đã chết";
    if (s == L"town") return L"Về thành";
    if (s == L"pb") return L"Phó bản" + (a.st.pb.empty() ? L"" : std::wstring(L" ") + (PbIndexC(a.st.pb) >= 0 ? PB_NAMEC[PbIndexC(a.st.pb)] : L"")) + (a.st.pbfloor.empty() ? L"" : L" " + a.st.pbfloor);
    if (s == L"resting") return L"Nghỉ " + std::to_wstring(a.st.rest > 0 ? a.st.rest : 0) + L"s";
    return s;
}
static void SetCell(int row, int col, const std::wstring& s) {
    LVITEMW it = {}; it.iSubItem = col; it.pszText = (LPWSTR)s.c_str();
    SendMessageW(g_acc, LVM_SETITEMTEXTW, row, (LPARAM)&it);
}
static std::wstring Money(long long v) {                     // 12345 -> "12.345"
    std::wstring s = std::to_wstring(v < 0 ? -v : v), o;
    int n = 0;
    for (int i = (int)s.size() - 1; i >= 0; --i) { o.insert(o.begin(), s[i]); if (++n % 3 == 0 && i > 0) o.insert(o.begin(), L'.'); }
    return (v < 0 ? L"-" : L"") + o;
}
static std::wstring Fmt1(double v);
static long long EpochNow() {
    FILETIME ft; GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u; u.LowPart = ft.dwLowDateTime; u.HighPart = ft.dwHighDateTime;
    return (long long)(u.QuadPart / 10000000ULL) - 11644473600LL;
}
static std::wstring EpochText(long long sec) {
    ULARGE_INTEGER u; u.QuadPart = (ULONGLONG)(sec + 11644473600LL) * 10000000ULL;
    FILETIME ft = { u.LowPart, u.HighPart }, lt; SYSTEMTIME t;
    FileTimeToLocalFileTime(&ft, &lt); FileTimeToSystemTime(&lt, &t);
    wchar_t b[40]; swprintf(b, 40, L"%02d-%02d-%04d %02d:%02d", t.wDay, t.wMonth, t.wYear, t.wHour, t.wMinute);
    return b;
}
static std::wstring VipTip(const Acc* a) {
    if (!a || !a->connected || !a->st.inGame) return L"Chưa có dữ liệu";
    if (!a->st.vip) return L"Không có trạng thái Võ Lâm Lệnh (VIP)";
    if (a->st.vipexp <= 0) return L"Có VIP (không đọc được thời hạn)";
    long long left = a->st.vipexp - EpochNow();
    std::wstring s = L"Hết hạn: " + EpochText(a->st.vipexp);
    if (left > 0) {
        long long d = left / 86400, h = left % 86400 / 3600, m = left % 3600 / 60;
        s += L" — còn " + (d ? std::to_wstring(d) + L" ngày " : L"") + std::to_wstring(h) + L" giờ" + (d ? L"" : L" " + std::to_wstring(m) + L" phút");
    } else s += L" — đã hết hạn";
    return s;
}
static void RefreshRow(Acc* a) {
    int row = IndexOf(a);
    if (row < 0) return;
    const Status& s = a->st;
    bool g = a->connected && s.inGame;
    SetCell(row, 1, g ? s.name : a->savedNv);
    SetCell(row, 2, StateText(*a) + (a->hidden ? L" (ẩn)" : L""));
    SetCell(row, 3, g ? std::to_wstring(s.lv) : L"");
    SetCell(row, 4, g ? std::to_wstring(s.hp) + L"/" + std::to_wstring(s.hpmax) : L"");
    SetCell(row, 5, g ? (s.mapname.empty() ? std::to_wstring(s.map) : s.mapname) + L" " + std::to_wstring(s.x) + L":" + std::to_wstring(s.y) : L"");
    SetCell(row, 6, g ? std::to_wstring(s.kills) + L" / " + std::to_wstring(s.deaths) + L" / " + std::to_wstring(s.picked) : L"");
    SetCell(row, 7, g ? (s.vip ? L"Có" : L"Không") : L"");
    SetCell(row, 8, g ? Money(s.mpick) : L"");
    SetCell(row, 9, a->connected && a->cpu >= 0 ? Fmt1(a->cpu) + L"%" : L"");
}
static void UpdateTip(HWND ctl, const std::wstring& text) {
    TOOLINFOW ti = { sizeof(ti) }; ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS; ti.hwnd = g_hwnd; ti.uId = (UINT_PTR)ctl;
    ti.lpszText = (LPWSTR)text.c_str();
    SendMessageW(g_tip, TTM_UPDATETIPTEXTW, 0, (LPARAM)&ti);
}
static void RefreshStats() {
    Acc* a = Sel();
    bool g = a && a->connected && a->st.inGame;
    const Status& s = a ? a->st : Status();
    SetText(IDC_LBL_VIP, !g ? L"VIP: -" : s.vip ? L"VIP: Có (rê chuột xem hạn)" : L"VIP: Không");
    UpdateTip(Item(IDC_LBL_VIP), VipTip(a));
    std::wstring ph;
    if (g && s.since > 60) ph = L" (" + Money(s.mpick * 3600 / s.since) + L"/giờ)";
    SetText(IDC_LBL_STAT1, g ? L"Đồng nhặt: " + Money(s.mpick) + ph + L"   Mất: " + std::to_wstring(s.mlost) + L"   Bị từ chối: " + std::to_wstring(s.prefused) : L"Đồng nhặt: -");
    SetText(IDC_LBL_STAT2, g ? L"Bán được: " + Money(s.msold) + L"   Sửa tốn: " + Money(s.mrep) + L"   Lãi: " + Money(s.mpick + s.msold - s.mrep) : L"");
    SetText(IDC_LBL_STAT3, g ? L"Túi trống: " + std::to_wstring(s.free) + L" ô   Độ bền thấp nhất: " + std::to_wstring(s.dur) + L"%" : L"");
    std::wstring bh = !g ? L"" : s.bh == L"stop" ? L"Bán/Hủy: ĐÃ DỪNG KHẨN CẤP" : s.bh == L"on" ? L"Bán/Hủy: đang bật (hủy/bán: " + s.bhn + L")" : L"Bán/Hủy: tắt";
    SetText(IDC_LBL_BHSTATE, bh);
    InvalidateRect(Item(IDC_LBL_VIP), nullptr, TRUE);
    InvalidateRect(Item(IDC_LBL_BHSTATE), nullptr, TRUE);
}
// ------------------------------------------------------------ pho ban
static std::wstring DateOnly() { return Today().substr(0, 10); }
static int PbIndex(const std::wstring& key) { for (int i = 0; i < NPB; ++i) if (key == PB_KEY[i]) return i; return -1; }
static void PbDayCheck(Acc* a) {
    if (a->cfg.pbDate == DateOnly()) return;
    a->cfg.pbDate = DateOnly();
    for (int i = 0; i < NPB; ++i) { a->cfg.pbDone[i] = 0; a->pbSkip[i].clear(); a->cfg.npcEnter[i] = -1; a->cfg.npcTotal[i] = -1; }
    SaveConfig(*a);
}
static bool PbRowFinished(const Acc* a, int i) {
    return !a->pbSkip[i].empty() || (a->cfg.npcTotal[i] > 0 && a->cfg.npcEnter[i] >= a->cfg.npcTotal[i]);
}
// Lien Tram / Me Cung: "lam khi lien tram duoi A hoac con it hon B phut"; Thien Quan / Doanh Trai / Phu Tu: "lam khi lien tram tu A va con tu B phut"
static const bool PB_LZ_GE[NPB] = { false, true, true, true, false };
/** du dieu kien chay luc nay (khung gio + buff lien tram); why = ly do cho */
static bool PbReady(const Acc* a, int i, std::wstring* why = nullptr) {
    const Config& c = a->cfg;
    if (c.pbTimeOn[i]) {
        SYSTEMTIME t; GetLocalTime(&t);
        int now = t.wHour * 60 + t.wMinute, s1 = c.pbH1[i] * 60 + c.pbM1[i], s2 = c.pbH2[i] * 60 + c.pbM2[i];
        bool in = s1 <= s2 ? (now >= s1 && now <= s2) : (now >= s1 || now <= s2);   // qua nua dem
        if (!in) { if (why) { wchar_t b[64]; swprintf(b, 64, L"ngoài giờ (%02d:%02d–%02d:%02d)", c.pbH1[i], c.pbM1[i], c.pbH2[i], c.pbM2[i]); *why = b; } return false; }
    }
    if (a->pbWaitAt[i] && GetTickCount() - a->pbWaitAt[i] < 60000) { if (why) *why = L"chưa đủ điều kiện liên trảm"; return false; }
    if (c.pbLzOn[i] && a->st.inGame && a->st.lzb != -2) {
        int v = a->st.lzb, m = a->st.lzbmin;
        bool ok;
        if (PB_LZ_GE[i]) ok = v >= c.pbLzVal[i] && m >= c.pbLzMin[i];
        else ok = v < 0 || v < c.pbLzVal[i] || m < c.pbLzMin[i];
        if (!ok) {
            if (why) *why = L"liên trảm " + (v < 0 ? std::wstring(L"?") : std::to_wstring(v)) + L", còn " + std::to_wstring(m) + L" phút";
            return false;
        }
    }
    return true;
}
static bool PbAnyOn(const Acc* a) {
    for (int i = 0; i < NPB; ++i) if (a->cfg.pbOn[i]) return true;
    return false;
}
static bool PbWanted(const Acc* a) {
    for (int i = 0; i < NPB; ++i) if (a->cfg.pbOn[i] && !PbRowFinished(a, i) && PbReady(a, i)) return true;
    return false;
}
static std::wstring PbStateText(const Acc* a, int i) {
    if (!a || !a->cfg.pbOn[i]) return L"Chưa làm";
    if (a->connected && a->st.state == L"pb" && a->st.pb == PB_KEY[i]) return L"Đang làm";
    if (!a->pbSkip[i].empty()) return a->pbSkip[i].find(L"hết lượt") != std::wstring::npos ? L"Đã xong" : L"Bỏ qua";
    if (a->cfg.npcTotal[i] > 0 && a->cfg.npcEnter[i] >= a->cfg.npcTotal[i]) return L"Đã xong";
    return L"Đang chờ";
}
/** data\phoban\<file>: <Point x=".." y=".." /> -> "x:y;x:y" (rong = dung diem mac dinh trong SWF) */
static std::wstring ReadRouteFile(const std::wstring& file) {
    std::wstring s = ReadUtf8File(ExeDir() + L"\\data\\phoban\\" + file), out;
    size_t pos = 0;
    while ((pos = s.find(L"<Point", pos)) != std::wstring::npos) {
        size_t e = s.find(L">", pos); if (e == std::wstring::npos) break;
        std::wstring tag = s.substr(pos, e - pos);
        auto attr = [&](const wchar_t* n) -> std::wstring {
            size_t a = tag.find(std::wstring(n) + L"=\""); if (a == std::wstring::npos) return L"";
            a += wcslen(n) + 2; size_t b = tag.find(L'"', a); return b == std::wstring::npos ? L"" : tag.substr(a, b - a);
        };
        std::wstring x = attr(L"x"), y = attr(L"y");
        if (!x.empty() && !y.empty()) out += (out.empty() ? L"" : L";") + std::to_wstring(ToInt(x)) + L":" + std::to_wstring(ToInt(y));
        pos = e;
    }
    return out;
}
static std::wstring ReadRoute(int i) { return ReadRouteFile(PB_FILE[i]); }
static std::wstring FightArgs(const Config& c);
static std::wstring PbArgs(Acc* a) {
    const Config& c = a->cfg;
    std::wstring list, extra, done;
    for (int i = 0; i < NPB; ++i) {
        done += (done.empty() ? L"" : L",") + std::wstring(PB_KEY[i]) + L":" + std::to_wstring(c.pbDone[i]);
        if (!c.pbOn[i] || PbRowFinished(a, i) || !PbReady(a, i)) continue;
        std::wstring k = PB_KEY[i];
        list += (list.empty() ? L"" : L",") + k;
        extra += L" " + k + L"_runs=0 " + k + L"_rev=" + std::to_wstring(c.pbRev[i]) + L" " + k + L"_range=" + std::to_wstring(c.pbRange[i]);
        {   // bo ky nang rieng cua pho ban nay
            int si = c.pbSet[i] < 0 ? c.trainSet : c.pbSet[i];
            si = (std::min)(4, (std::max)(0, si));
            const Config::SkSet& ss = c.sets[si];
            std::wstring heal; for (wchar_t ch : ss.heal) if ((ch >= L'0' && ch <= L'9') || ch == L',' || ch == L':') heal += ch;
            extra += L" " + k + L"_set=" + std::to_wstring(si + 1);
            for (int g = 0; g < 4; ++g) extra += L" " + k + L"_g" + std::to_wstring(g + 1) + L"=" + NumList(ss.g[g]);
            extra += L" " + k + L"_heal=" + heal + L" " + k + L"_buff=" + NumList(ss.buff) + L" " + k + L"_supmp=" + std::to_wstring(ss.supmp);
        }
        if (i == 1) extra += std::wstring(L" tq_afk=") + (c.tqAfk ? L"1" : L"0") + L" tq_bosscount=" + (c.tqBossCount ? L"1" : L"0");
        if (c.pbLzOn[i]) extra += L" " + k + L"_lz=" + (PB_LZ_GE[i] ? L"ge:" : L"lt:") + std::to_wstring(c.pbLzVal[i]) + L":" + std::to_wstring(c.pbLzMin[i]);
        if (c.pbSfOn[i] && (i == 1 || i == 2) && c.pbSf[i] > 0) extra += L" " + k + L"_sf=" + std::to_wstring(c.pbSf[i]);
        if (c.pbNmOn[i] && (i == 1 || i == 2 || i == 3)) extra += L" " + k + L"_nomob=" + std::to_wstring(c.pbNm[i]);
        if (i == 0) extra += L" lt_y=" + std::to_wstring(c.ltStopOn ? c.ltY : 0) + L" lt_cont=" + (c.ltCont ? L"1" : L"0") +
                             L" lt_lure=" + (c.ltLureOn ? L"1" : L"0") + L" lt_x=" + std::to_wstring(c.ltX) + L" lt_kpp=" + std::to_wstring(c.ltKpp) +
                             L" lt_pick=" + std::to_wstring(c.ltPick) + L" lt_skipboss=" + (c.ltSkipBoss ? L"1" : L"0");
        if (i == 1) extra += L" tq_minr=" + std::to_wstring(c.tqMinR);
        if (i == 2) extra += std::wstring(L" dt_jump=") + (c.dtJump ? L"1" : L"0") + L" dt_jmax=" + (c.dtJmax == 1 ? L"500" : L"0");
        if (i == 3) extra += std::wstring(L" pt_jump=") + (c.ptJump ? L"1" : L"0") + L" pt_bow=" + (c.ptBow ? L"1" : L"0");
        if (i == 4) {
            extra += L" mc_farm=" + std::to_wstring(c.mcFarm) + L" mc_farmmin=" + std::to_wstring(c.mcFarmMin)
                   + L" mc_farmby=" + (c.mcFarmBy == 1 ? L"lz" : L"min") + L" mc_farmlz=" + std::to_wstring(c.mcFarmLz)
                   + L" mc_skip=" + (c.mcSkip ? L"1" : L"0");
            std::wstring sr = ReadRouteFile(L"MeCungThanBi.xml"), s15 = ReadRouteFile(L"MeCung15.xml");
            if (!s15.empty()) extra += L" mc_r15=" + s15;
            if (!sr.empty()) extra += L" mc_sroute=" + sr;
        }
        std::wstring r = ReadRoute(i);
        if (!r.empty()) extra += L" " + k + L"_route=" + r;
    }
    std::wstring on;                                            // moi pho ban dang tick (pb_list: chi thoat pho ban dang lam khi bo tick dung no)
    for (int i = 0; i < NPB; ++i) if (c.pbOn[i]) on += (on.empty() ? L"" : L",") + std::wstring(PB_KEY[i]);
    return L"list=" + list + L" on=" + on + extra + L" done=" + done + FightArgs(c);
}

/** o trang thai canh "Danh quai": Chua lam / Dang cho / Dang lam / Dang nghi / Ve thanh */
static std::wstring TrainStateText(const Acc* a) {
    if (!a || !a->cfg.enabled) return L"Chưa làm";
    if (!a->active || !a->connected || !a->st.inGame || PbWanted(a) || a->st.state == L"pb") return L"Đang chờ";
    return a->st.state == L"resting" ? L"Đang nghỉ" : a->st.state == L"town" ? L"Về thành" : L"Đang làm";
}
static void RefreshTrainState() {
    Acc* a = Sel();
    g_loading = true;
    Check(IDC_CHK_TRAIN, a && a->cfg.enabled);
    g_loading = false;
    EnableWindow(Item(IDC_CHK_TRAIN), a != nullptr);
    std::wstring t = TrainStateText(a);
    SetWindowTextW(g_trainState, t.c_str());
    InvalidateRect(g_trainState, nullptr, TRUE);
    for (int i = 0; i < NPB; ++i) {
        g_loading = true; Check(IDC_CHK_PB0 + i, a && a->cfg.pbOn[i]); g_loading = false;
        EnableWindow(Item(IDC_CHK_PB0 + i), a != nullptr);
        std::wstring pt = PbStateText(a, i);
        if (pt == L"Đang làm" && !a->st.pbfloor.empty()) pt += L" " + a->st.pbfloor;
        wchar_t cur[64]; GetWindowTextW(g_pbState[i], cur, 64);
        if (pt != cur) { SetWindowTextW(g_pbState[i], pt.c_str()); InvalidateRect(g_pbState[i], nullptr, TRUE); }
        std::wstring wy;
        bool wait = a && a->cfg.pbOn[i] && !PbRowFinished(a, i) && !PbReady(a, i, &wy);
        std::wstring dn = a ? (a->cfg.npcTotal[i] >= 0 ? L"Hôm nay: đã đi " + std::to_wstring(a->cfg.npcEnter[i]) + L"/" + std::to_wstring(a->cfg.npcTotal[i]) + L" lượt (theo NPC)"
                                                   : std::wstring(L"Hôm nay: chưa đọc số lượt từ NPC")) +
                              (a->pbSkip[i].empty() ? L"" : L" — " + a->pbSkip[i]) + (wait ? L" — chờ: " + wy : L"") : L"";
        SetText(IDC_LBL_PBDONE0 + i, dn);
    }
    {
        std::wstring inf;
        if (a && a->st.inGame) {
            inf = L"Liên trảm hiện tại: " + std::to_wstring(a->st.lz) + L"   Buff liên trảm: ";
            inf += a->st.lzb == -2 ? L"-" : a->st.lzb == 0 ? L"không có" : (a->st.lzb < 0 ? std::wstring(L"?") : std::to_wstring(a->st.lzb)) + L", còn " + std::to_wstring(a->st.lzbmin) + L" phút";
            if (a->st.lzmax >= 0) {
                wchar_t g[24]; swprintf(g, 24, L"%.1f", a->st.lzgap / 10.0);
                inf += L"\r\nLượt này: cao nhất " + std::to_wstring(a->st.lzmax) + L", đứt " + std::to_wstring(a->st.lzbrk) + L" lần, cứu chuỗi " +
                       std::to_wstring(a->st.lzresc) + L" lần, lâu nhất giữa 2 lần giết " + g + L"s";
            }
        }
        SetText(PBX(0, PX_INFO), inf);
    }
    RefreshStats();
}
static void RefreshConnInfo() {
    int on = 0;
    for (auto& a : g_accs) if (a->connected) ++on;
    SetText(IDC_CONNINFO, std::to_wstring(on) + L"/" + std::to_wstring(g_accs.size()) + L" cửa sổ đang kết nối");
}

// ------------------------------------------------------------ cac trang cai dat

static void LvText(HWND lv, int row, int col, const std::wstring& s) {
    LVITEMW it = {}; it.iSubItem = col; it.pszText = (LPWSTR)s.c_str(); SendMessageW(lv, LVM_SETITEMTEXTW, row, (LPARAM)&it);
}
static int LvInsert(HWND lv, int row, const std::wstring& s) {
    LVITEMW it = {}; it.mask = LVIF_TEXT; it.iItem = row; it.pszText = (LPWSTR)s.c_str();
    return (int)SendMessageW(lv, LVM_INSERTITEMW, 0, (LPARAM)&it);
}

static void RefreshPoints() {
    g_loading = true;
    SendMessageW(g_pts, LVM_DELETEALLITEMS, 0, 0);
    if (Acc* a = Sel()) {
        for (size_t i = 0; i < a->cfg.pts.size(); ++i) {
            const TrainPoint& p = a->cfg.pts[i];
            int row = LvInsert(g_pts, (int)i, std::to_wstring(i + 1));
            LvText(g_pts, row, 1, (p.name.empty() ? L"Map " + std::to_wstring(p.map) : p.name) + L" - {" + std::to_wstring(p.x) + L":" + std::to_wstring(p.y) + L"}");
            LvText(g_pts, row, 2, std::to_wstring(p.r));
            ListView_SetCheckState(g_pts, row, p.on);
        }
    }
    g_loading = false;
}

// Danh sach skill cua acc (lenh "skills"): id:ten:tam:useway:public_type:public_time
struct SkillRow { std::wstring id, name, dist, group, ptype, ptime; };
static std::map<std::wstring, std::vector<SkillRow>> g_skills;

static std::wstring SkillGroup(const std::wstring& id) {
    int v = ToInt(id) / 1000;
    if (v == 51) return L"Môn phái";
    if (v == 52) return L"Giang hồ";
    if (v == 53) return L"Bang phái";
    return L"Khác";
}
static bool HasId(const std::wstring& list, const std::wstring& id) {
    return (L"," + list + L",").find(L"," + id + L",") != std::wstring::npos;
}
static int SkillGroupIdx(const std::wstring& id) {
    int v = ToInt(id) / 1000;
    return v == 51 ? 0 : v == 52 ? 1 : v == 53 ? 2 : 3;
}
static std::vector<std::wstring> IdList(const std::wstring& s) {
    std::vector<std::wstring> v; std::wstring l = NumList(s); size_t i = 0;
    while (i < l.size()) { size_t e = l.find(L',', i); if (e == std::wstring::npos) e = l.size(); if (e > i) v.push_back(l.substr(i, e - i)); i = e + 1; }
    return v;
}
static std::wstring JoinIds(const std::vector<std::wstring>& v) { std::wstring o; for (auto& x : v) o += (o.empty() ? L"" : L",") + x; return o; }
static std::wstring SkillNameOf(const Acc* a, const std::wstring& id) {
    if (a && g_skills.count(a->id)) for (auto& r : g_skills[a->id]) if (r.id == id) return r.name;
    return L"#" + id;
}
struct SupRow { bool heal; std::wstring id; int pct; };
static std::vector<SupRow> SupRows(const Config::SkSet& ss) {
    std::vector<SupRow> v;
    size_t i = 0; std::wstring h = ss.heal;
    while (i < h.size()) {
        size_t e = h.find(L',', i); if (e == std::wstring::npos) e = h.size();
        std::wstring one = h.substr(i, e - i); size_t c = one.find(L':');
        if (c != std::wstring::npos) v.push_back({ true, NumList(one.substr(0, c)), ToInt(one.substr(c + 1), 50) });
        i = e + 1;
    }
    for (auto& id : IdList(ss.buff)) v.push_back({ false, id, 0 });
    return v;
}
static void SetSupRows(Config::SkSet& ss, const std::vector<SupRow>& v) {
    ss.heal.clear(); ss.buff.clear();
    for (auto& r : v) {
        if (r.heal) ss.heal += (ss.heal.empty() ? L"" : L",") + r.id + L":" + std::to_wstring(r.pct);
        else ss.buff += (ss.buff.empty() ? L"" : L",") + r.id;
    }
}
static void RefreshSkillPage() {
    Acc* a = Sel();
    Config c = a ? a->cfg : Config();
    const Config::SkSet& ss = c.sets[g_skSet];
    int sk = TabCtrl_GetCurSel(g_tabSk);
    g_loading = true;
    SetSel(IDC_CMB_SKSET, g_skSet);
    if (sk < 4) {
        std::vector<std::wstring> ids = IdList(ss.g[sk]);
        SendMessageW(g_skOrder, LVM_DELETEALLITEMS, 0, 0);
        for (size_t i = 0; i < ids.size(); ++i) {
            int r = LvInsert(g_skOrder, (int)i, std::to_wstring(i + 1));
            LvText(g_skOrder, r, 1, SkillNameOf(a, ids[i]) + L" (" + ids[i] + L")");
        }
        HWND cb = Item(IDC_CMB_SKADD);
        SendMessageW(cb, CB_RESETCONTENT, 0, 0);
        SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)L"Mời bạn chọn");
        if (a && g_skills.count(a->id))
            for (auto& r : g_skills[a->id])
                if (SkillGroupIdx(r.id) == sk && std::find(ids.begin(), ids.end(), r.id) == ids.end()) {
                    std::wstring t = r.name + L" (" + r.id + L")";
                    SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)t.c_str());
                }
        SendMessageW(cb, CB_SETCURSEL, 0, 0);
    } else {
        SendMessageW(g_supList, LVM_DELETEALLITEMS, 0, 0);
        auto rows = SupRows(ss);
        for (size_t i = 0; i < rows.size(); ++i) {
            int r = LvInsert(g_supList, (int)i, rows[i].heal ? L"Hồi máu" : L"Buff");
            LvText(g_supList, r, 1, SkillNameOf(a, rows[i].id) + L" (" + rows[i].id + L")");
            LvText(g_supList, r, 2, rows[i].heal ? L"máu dưới " + std::to_wstring(rows[i].pct) + L"%" : L"khi hết buff");
        }
        HWND cb = Item(IDC_CMB_SUPSKILL);
        SendMessageW(cb, CB_RESETCONTENT, 0, 0);
        SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)L"Mời bạn chọn");
        if (a && g_skills.count(a->id))
            for (auto& r : g_skills[a->id]) { std::wstring t = r.name + L" (" + r.id + L")"; SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)t.c_str()); }
        SendMessageW(cb, CB_SETCURSEL, 0, 0);
        SetText(IDC_EDIT_SUPMP, std::to_wstring(ss.supmp));
        EnableWindow(Item(IDC_EDIT_SUPPCT), CurSel(IDC_CMB_SUPTYPE) == 0);
    }
    g_loading = false;
}
/** ID skill tu dong "Ten (id)" cua combo */
static std::wstring ComboId(int ctl) {
    HWND cb = Item(ctl); int i = (int)SendMessageW(cb, CB_GETCURSEL, 0, 0);
    if (i <= 0) return L"";
    int n = (int)SendMessageW(cb, CB_GETLBTEXTLEN, i, 0); std::wstring t((size_t)n, L'\0');
    SendMessageW(cb, CB_GETLBTEXT, i, (LPARAM)&t[0]);
    size_t a = t.rfind(L'('), b = t.rfind(L')');
    return a == std::wstring::npos || b == std::wstring::npos ? L"" : NumList(t.substr(a + 1, b - a - 1));
}
static void SkillSetChanged(Acc* a) {
    SaveConfig(*a);
    if (a->cfg.enabled && a->connected && a->st.inGame && a->cfg.trainSet == g_skSet) a->restartAt = GetTickCount() + 1500;
    RefreshSkillPage();
}
/** tu dien skill ho tro (theo ten) cho nhom 1 lan dau lay danh sach skill */
static void DefaultSupport(Acc* a) {
    Config::SkSet& ss = a->cfg.sets[0];
    if (!ss.heal.empty() || !ss.buff.empty() || !g_skills.count(a->id)) return;
    std::vector<SupRow> v;
    for (auto& r : g_skills[a->id]) {
        if (r.name == L"Cách Không Độ Khí" || r.name == L"Từ Hàng Phổ Độ") v.push_back({ true, r.id, 70 });
        if (r.name == L"Chiến Ý Kích Ngang" || r.name == L"Võ Thần Lâm Thể") v.push_back({ false, r.id, 0 });
    }
    if (v.empty()) return;
    SetSupRows(ss, v);
    SaveConfig(*a);
    AddLog(a, L"Kỹ năng - Đã thêm " + std::to_wstring(v.size()) + L" skill hỗ trợ vào Nhóm kỹ năng 1 (tab HỖ TRỢ, sửa được)");
}
static void RefreshDrugs(const Config& c) {
    g_loading = true;
    for (int i = 0; i < NDRUGS; ++i) ListView_SetCheckState(g_drugs, i, HasId(c.drugIds, std::to_wstring(DRUGS[i].id)));
    g_loading = false;
}
static std::vector<int> RuleRows(const Config& c, int act) {
    std::vector<int> v; for (size_t i = 0; i < c.rules.size(); ++i) if (c.rules[i].act == act) v.push_back((int)i); return v;
}
static void RefreshRules(const Config& c) {
    g_loading = true;
    for (int act = 0; act < 2; ++act) {
        HWND lv = act == 0 ? g_rules : g_rules2;
        SendMessageW(lv, LVM_DELETEALLITEMS, 0, 0);
        auto rows = RuleRows(c, act);
        for (size_t k = 0; k < rows.size(); ++k) {
            const Rule& ru = c.rules[rows[k]];
            int r = LvInsert(lv, (int)k, std::to_wstring(k + 1));
            LvText(lv, r, 1, ru.real ? L"Làm thật" : L"Chạy thử");
            LvText(lv, r, 2, RuleText(ru));
            ListView_SetCheckState(lv, r, ru.real);
        }
    }
    g_loading = false;
}
static void UpdateRuleEditor() {
    bool item = CurSel(IDC_CMB_R_KIND) == 1;
    for (int id : { IDC_CMB_R_Q, IDC_EDIT_R_S, IDC_CMB_R_LV, IDC_CMB_R_LOCK }) EnableWindow(Item(id), !item);
    for (int i = 0; i < NSECTS; ++i) EnableWindow(Item(IDC_CHK_SECT0 + i), !item);
    for (int i = 0; i < NSLOTS; ++i) EnableWindow(Item(IDC_CHK_SLOT0 + i), !item);
    EnableWindow(Item(IDC_EDIT_R_NAMES), item);
}

static void LoadUI() {
    Acc* a = Sel();
    g_loading = true;
    Config c = a ? a->cfg : Config();
    Check(IDC_CHK_T1, c.tNormal); Check(IDC_CHK_T2, c.tElite); Check(IDC_CHK_T3, c.tBoss);
    SetSel(IDC_CMB_SKMODE, c.skillMode);
    SetText(IDC_EDIT_KEYS, c.keys);
    EnableWindow(Item(IDC_EDIT_KEYS), c.skillMode == 1);
    SetSel(IDC_CMB_TRAINSET, c.trainSet);
    SetSel(IDC_CMB_PKMODE, c.pickMode);
    SetText(IDC_EDIT_PKLIST, c.pickList);
    SetSel(IDC_CMB_POTMODE, c.potMode);
    SetText(IDC_EDIT_HPKEY, c.hpkey); SetText(IDC_EDIT_MPKEY, c.mpkey);
    SetText(IDC_EDIT_HP, std::to_wstring(c.hp)); SetText(IDC_EDIT_MP, std::to_wstring(c.mp));
    Check(IDC_CHK_REST, c.restOn);
    SetText(IDC_EDIT_RESTN, std::to_wstring(c.restDeaths));
    SetText(IDC_EDIT_RESTMIN, std::to_wstring(c.restMin));
    Check(IDC_CHK_SWTIME, c.swTimeOn);
    SetText(IDC_EDIT_SWMIN, std::to_wstring(c.swMin));
    Check(IDC_CHK_SWDEATH, c.swDeath);
    Check(IDC_CHK_REPAIR, c.repairOn); SetSel(IDC_CMB_RTYPE, c.repairType == 2 ? 1 : 0);
    Check(IDC_CHK_BH, c.bhOn);
    SetText(IDC_EDIT_FREEE, std::to_wstring(c.freeE)); SetText(IDC_EDIT_FREEI, std::to_wstring(c.freeI));
    Check(IDC_CHK_FLOORQ, c.floorQ); SetText(IDC_EDIT_FLOORS, std::to_wstring(c.floorS));
    SetText(IDC_EDIT_PROTECT, c.protect);
    Check(IDC_CHK_TELE, c.tele); Check(IDC_CHK_VLL, c.vllAuto);
    for (int i = 0; i < 13; ++i) Check(IDC_CHK_GS0 + i, c.gs[i]);
    Check(IDC_CHK_HPFX, c.hpfx); Check(IDC_CHK_CLOSEUI, c.closeUi);
    for (int i = 0; i < NPB; ++i) { SetText(IDC_EDIT_PBRUNS0 + i, std::to_wstring(c.pbRange[i])); SetText(IDC_EDIT_PBREV0 + i, std::to_wstring(c.pbRev[i])); }
    Check(PBX(1, PX_TQAFK), c.tqAfk); Check(PBX(1, PX_TQBC), c.tqBossCount);
    for (int i = 0; i < NPB; ++i) SetSel(PBX(i, PX_SET), c.pbSet[i] + 1);
    SetText(IDC_EDIT_TQMINR, std::to_wstring(c.tqMinR)); Check(IDC_CHK_DTJUMP, c.dtJump); SetSel(IDC_CMB_DTJMAX, c.dtJmax);
    Check(IDC_CHK_PTJUMP, c.ptJump); Check(IDC_CHK_PTBOW, c.ptBow);
    SetSel(IDC_CMB_MCFARM, c.mcFarm); SetText(IDC_EDIT_MCMIN, std::to_wstring(c.mcFarmMin));
    SetSel(IDC_CMB_MCBY, c.mcFarmBy); SetText(IDC_EDIT_MCLZ, std::to_wstring(c.mcFarmLz)); Check(IDC_CHK_MCSKIP, c.mcSkip); SetSel(IDC_CMB_HIDELV, c.hideLevel == 2 ? 1 : 0);
    for (int i = 0; i < NPB; ++i) {
        Check(PBX(i, PX_TIMEON), c.pbTimeOn[i]);
        SetText(PBX(i, PX_H1), std::to_wstring(c.pbH1[i])); SetText(PBX(i, PX_M1), std::to_wstring(c.pbM1[i]));
        SetText(PBX(i, PX_H2), std::to_wstring(c.pbH2[i])); SetText(PBX(i, PX_M2), std::to_wstring(c.pbM2[i]));
        Check(PBX(i, PX_LZON), c.pbLzOn[i]); SetText(PBX(i, PX_LZVAL), std::to_wstring(c.pbLzVal[i])); SetText(PBX(i, PX_LZMIN), std::to_wstring(c.pbLzMin[i]));
        if (i == 1 || i == 2) { Check(PBX(i, PX_SFON), c.pbSfOn[i]); SetText(PBX(i, PX_SF), std::to_wstring(c.pbSf[i])); }
        if (i == 1 || i == 2 || i == 3) { Check(PBX(i, PX_NMON), c.pbNmOn[i]); SetText(PBX(i, PX_NM), std::to_wstring(c.pbNm[i])); }
    }
    Check(PBX(0, PX_LTSTOP), c.ltStopOn); SetText(PBX(0, PX_LTY), std::to_wstring(c.ltY)); Check(PBX(0, PX_LTCONT), c.ltCont);
    Check(PBX(0, PX_LTLURE), c.ltLureOn); SetText(PBX(0, PX_LTX), std::to_wstring(c.ltX)); SetText(PBX(0, PX_LTKPP), std::to_wstring(c.ltKpp));
    SetSel(PBX(0, PX_LTPICK), c.ltPick); Check(PBX(0, PX_LTSKIP), c.ltSkipBoss);
    g_loading = false;
    RefreshSkillPage();
    RefreshDrugs(c);
    RefreshRules(c);
    RefreshPoints();
    RefreshTrainState();
    RefreshLog();
    if (!g_newAcc) LoadAccForm();
}

static void ReadUI(Config& c) {
    c.tNormal = Checked(IDC_CHK_T1); c.tElite = Checked(IDC_CHK_T2); c.tBoss = Checked(IDC_CHK_T3);
    c.skillMode = CurSel(IDC_CMB_SKMODE) == 1 ? 1 : 0;
    c.keys = GetText(IDC_EDIT_KEYS);
    EnableWindow(Item(IDC_EDIT_KEYS), c.skillMode == 1);
    c.trainSet = CurSel(IDC_CMB_TRAINSET);
    c.pickMode = CurSel(IDC_CMB_PKMODE);
    c.pickList = GetText(IDC_EDIT_PKLIST);
    for (auto& ch : c.pickList) if (ch == L'\r' || ch == L'\n') ch = L' ';
    c.potMode = CurSel(IDC_CMB_POTMODE);
    c.hpkey = GetText(IDC_EDIT_HPKEY); c.mpkey = GetText(IDC_EDIT_MPKEY);
    c.hp = (std::min)(95, (std::max)(1, ToInt(GetText(IDC_EDIT_HP), 40)));
    c.mp = (std::min)(95, (std::max)(1, ToInt(GetText(IDC_EDIT_MP), 20)));
    c.restOn = Checked(IDC_CHK_REST);
    c.restDeaths = (std::max)(1, ToInt(GetText(IDC_EDIT_RESTN), 10));
    c.restMin = (std::max)(1, ToInt(GetText(IDC_EDIT_RESTMIN), 5));
    c.swTimeOn = Checked(IDC_CHK_SWTIME);
    c.swMin = (std::max)(1, ToInt(GetText(IDC_EDIT_SWMIN), 30));
    c.swDeath = Checked(IDC_CHK_SWDEATH);
    c.repairOn = Checked(IDC_CHK_REPAIR); c.repairType = CurSel(IDC_CMB_RTYPE) == 1 ? 2 : 1;
    c.bhOn = Checked(IDC_CHK_BH);
    c.freeE = (std::max)(0, ToInt(GetText(IDC_EDIT_FREEE), 5)); c.freeI = (std::max)(0, ToInt(GetText(IDC_EDIT_FREEI), 5));
    c.floorQ = Checked(IDC_CHK_FLOORQ);
    c.floorS = (std::max)(0, ToInt(GetText(IDC_EDIT_FLOORS), 1));
    c.protect = GetText(IDC_EDIT_PROTECT);
    for (auto& ch : c.protect) if (ch == L'\r' || ch == L'\n') ch = L' ';
    c.tele = Checked(IDC_CHK_TELE); c.vllAuto = Checked(IDC_CHK_VLL);
    for (int i = 0; i < 13; ++i) c.gs[i] = Checked(IDC_CHK_GS0 + i);
    c.hpfx = Checked(IDC_CHK_HPFX); c.closeUi = Checked(IDC_CHK_CLOSEUI);
    for (int i = 0; i < NPB; ++i) {
        c.pbRange[i] = (std::min)(999, (std::max)(1, ToInt(GetText(IDC_EDIT_PBRUNS0 + i), 99)));
        c.pbRev[i] = (std::max)(0, ToInt(GetText(IDC_EDIT_PBREV0 + i), 2));
    }
    c.tqMinR = (std::max)(0, ToInt(GetText(IDC_EDIT_TQMINR), 15)); c.dtJump = Checked(IDC_CHK_DTJUMP); c.dtJmax = CurSel(IDC_CMB_DTJMAX) == 1 ? 1 : 0;
    c.ptJump = Checked(IDC_CHK_PTJUMP); c.ptBow = Checked(IDC_CHK_PTBOW);
    c.mcFarm = (std::max)(0, CurSel(IDC_CMB_MCFARM)); c.mcFarmMin = (std::max)(1, ToInt(GetText(IDC_EDIT_MCMIN), 10));
    c.mcFarmBy = CurSel(IDC_CMB_MCBY) == 1 ? 1 : 0; c.mcFarmLz = (std::max)(1, ToInt(GetText(IDC_EDIT_MCLZ), 300)); c.mcSkip = Checked(IDC_CHK_MCSKIP); c.hideLevel = CurSel(IDC_CMB_HIDELV) == 1 ? 2 : 1;
    for (int i = 0; i < NPB; ++i) {
        c.pbTimeOn[i] = Checked(PBX(i, PX_TIMEON));
        c.pbH1[i] = (std::min)(23, ToInt(GetText(PBX(i, PX_H1)), 0)); c.pbM1[i] = (std::min)(59, ToInt(GetText(PBX(i, PX_M1)), 0));
        c.pbH2[i] = (std::min)(23, ToInt(GetText(PBX(i, PX_H2)), 23)); c.pbM2[i] = (std::min)(59, ToInt(GetText(PBX(i, PX_M2)), 59));
        c.pbLzOn[i] = Checked(PBX(i, PX_LZON)); c.pbLzVal[i] = (std::max)(0, ToInt(GetText(PBX(i, PX_LZVAL)), 0)); c.pbLzMin[i] = (std::max)(0, ToInt(GetText(PBX(i, PX_LZMIN)), 0));
        if (i == 1 || i == 2) { c.pbSfOn[i] = Checked(PBX(i, PX_SFON)); c.pbSf[i] = (std::max)(0, ToInt(GetText(PBX(i, PX_SF)), 0)); }
        if (i == 1 || i == 2 || i == 3) { c.pbNmOn[i] = Checked(PBX(i, PX_NMON)); c.pbNm[i] = (std::max)(1, ToInt(GetText(PBX(i, PX_NM)), 3)); }
    }
    c.tqAfk = Checked(PBX(1, PX_TQAFK)); c.tqBossCount = Checked(PBX(1, PX_TQBC));
    for (int i = 0; i < NPB; ++i) c.pbSet[i] = (std::min)(4, CurSel(PBX(i, PX_SET)) - 1);
    c.ltStopOn = Checked(PBX(0, PX_LTSTOP)); c.ltY = (std::max)(1, ToInt(GetText(PBX(0, PX_LTY)), 500)); c.ltCont = Checked(PBX(0, PX_LTCONT));
    c.ltLureOn = Checked(PBX(0, PX_LTLURE)); c.ltX = (std::max)(0, ToInt(GetText(PBX(0, PX_LTX)), 100));
    c.ltKpp = (std::min)(5, (std::max)(1, ToInt(GetText(PBX(0, PX_LTKPP)), 2))); c.ltPick = CurSel(PBX(0, PX_LTPICK)) == 1 ? 1 : 0; c.ltSkipBoss = Checked(PBX(0, PX_LTSKIP));
}

// ------------------------------------------------------------ tab TAI KHOAN

static const XmlAcc* FindXml(const std::vector<XmlAcc>& v, const std::wstring& user) {
    for (auto& x : v) if (_wcsicmp(x.user.c_str(), user.c_str()) == 0) return &x;
    return nullptr;
}
static void SetPassVisible(bool on) {
    g_showPass = on;
    SendMessageW(Item(IDC_EDIT_A_PASS), EM_SETPASSWORDCHAR, on ? 0 : (WPARAM)L'\x25CF', 0);
    SetText(IDC_BTN_A_EYE, on ? L"Ẩn" : L"Hiện");
    InvalidateRect(Item(IDC_EDIT_A_PASS), nullptr, TRUE);
}
static void LoadAccForm() {
    Acc* a = g_newAcc ? nullptr : Sel();
    XmlAcc x;
    if (a) { auto v = LoadXmlAccounts(); if (auto* f = FindXml(v, a->id)) x = *f; else x.user = a->id; }
    g_loading = true;
    SetText(IDC_LBL_A_TITLE, g_newAcc ? L"CỬA SỔ MỚI — nhập tài khoản" : L"TÀI KHOẢN ĐĂNG NHẬP GAME");
    SetText(IDC_EDIT_A_USER, x.user);
    SendMessageW(Item(IDC_EDIT_A_USER), EM_SETREADONLY, g_newAcc ? FALSE : TRUE, 0);
    SetText(IDC_EDIT_A_PASS, DecPass(x.pass));
    SetText(IDC_EDIT_A_SV, x.sv.empty() ? L"1" : x.sv);
    SetSel(IDC_CMB_A_KENH, (std::min)(8, (std::max)(1, KenhNum(x.kenh))) - 1);
    SetText(IDC_EDIT_A_NV, x.nv);
    ShowWindow(Item(IDC_BTN_A_CANCEL), g_newAcc && TabCtrl_GetCurSel(g_tabMain) == 3 ? SW_SHOW : SW_HIDE);
    EnableWindow(Item(IDC_BTN_A_SAVE), g_newAcc || a != nullptr);
    EnableWindow(Item(IDC_BTN_A_LOGIN), g_newAcc || a != nullptr);
    g_loading = false;
    SetPassVisible(false);
}
static void AddAcc(const std::wstring& id);
static bool SaveAccForm(std::wstring* savedUser) {
    std::wstring user = GetText(IDC_EDIT_A_USER), pass = GetText(IDC_EDIT_A_PASS), sv = GetText(IDC_EDIT_A_SV);
    size_t a = user.find_first_not_of(L' '), b = user.find_last_not_of(L' ');
    user = a == std::wstring::npos ? L"" : user.substr(a, b - a + 1);
    if (user.empty() || user.find_first_of(L" <>&\\/") != std::wstring::npos) {
        MessageBoxW(g_hwnd, L"Tài khoản trống hoặc có ký tự không hợp lệ.", L"Chưa lưu", MB_ICONWARNING); return false;
    }
    if (pass.empty()) { MessageBoxW(g_hwnd, L"Chưa nhập mật khẩu.", L"Chưa lưu", MB_ICONWARNING); return false; }
    auto v = LoadXmlAccounts();
    const XmlAcc* old = FindXml(v, user);
    if (g_newAcc && old) { MessageBoxW(g_hwnd, L"Tài khoản này đã có trong danh sách.", L"Chưa lưu", MB_ICONWARNING); return false; }
    XmlAcc x = old ? *old : XmlAcc();
    x.user = old ? old->user : user;
    if (DecPass(x.pass) != pass) x.pass = EncPass(pass);        // chi ma hoa lai khi doi mat khau
    if (x.pass.empty()) { MessageBoxW(g_hwnd, L"Không mã hóa được mật khẩu (DPAPI).", L"Chưa lưu", MB_ICONERROR); return false; }
    x.sv = std::to_wstring((std::max)(1, ToInt(sv, 1)));
    x.kenh = L"Kênh " + std::to_wstring(CurSel(IDC_CMB_A_KENH) + 1);
    if (!SaveXmlAccount(x)) { MessageBoxW(g_hwnd, (L"Không ghi được " + AccXmlPath()).c_str(), L"Chưa lưu", MB_ICONERROR); return false; }
    if (savedUser) *savedUser = x.user;
    bool wasNew = g_newAcc;
    g_newAcc = false;
    AddAcc(x.user);
    if (Acc* acc = [&]() -> Acc* { for (auto& p : g_accs) if (_wcsicmp(p->id.c_str(), x.user.c_str()) == 0) return p.get(); return nullptr; }()) {
        acc->inXml = true;
        AddLog(acc, std::wstring(wasNew ? L"Đã thêm tài khoản " : L"Đã lưu tài khoản ") + x.user + L" (máy chủ " + x.sv + L", " + x.kenh + L")");
        int row = -1; for (size_t i = 0; i < g_accs.size(); ++i) if (g_accs[i].get() == acc) row = (int)i;
        if (row >= 0) ListView_SetItemState(g_acc, row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    }
    LoadAccForm();
    return true;
}
static bool g_syncTick = false;             // dang dat o tick bang code: bo qua LVN_ITEMCHANGED
/** dat o tick tai khoan theo a->active, khong kich hoat mo client / dung chuc nang */
static void SyncTick(Acc* a) {
    int row = IndexOf(a);
    if (row < 0) return;
    g_syncTick = true;
    ListView_SetCheckState(g_acc, row, a->active ? TRUE : FALSE);
    g_syncTick = false;
    if (a == Sel()) RefreshTrainState();
}
static void MarkActive(Acc* a) { a->active = true; a->manualOff = false; SyncTick(a); }
/** mo cua so vlcmhost rieng cho tai khoan (hoac dang nhap lai neu cua so do dang mo). false = khong mo duoc */
static bool LoginAcc(Acc* a) {
    if (!a) return false;
    if (a->connected) {
        a->pending.push_back({ L"reload", GetTickCount() });
        a->link->Send(L"reload");
        AddLog(a, L"Đăng nhập lại (cửa sổ game đang mở)");
        MarkActive(a);
        return true;
    }
    auto v = LoadXmlAccounts();
    const XmlAcc* x = FindXml(v, a->id);
    if (!x || x->pass.empty()) {
        AddLog(a, L"Chưa có mật khẩu — nhập ở tab TÀI KHOẢN rồi bấm Lưu");
        g_pbPane = -1; TabCtrl_SetCurSel(g_tabMain, 3); UpdatePages(); LoadAccForm();
        return false;
    }
    std::wstring exe = ExeDir() + L"\\vlcmhost.exe";
    if (GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES) {
        MessageBoxW(g_hwnd, (L"Không thấy " + exe + L"\r\nĐặt vlcmpanel.exe cùng thư mục với vlcmhost.exe.").c_str(), L"Không mở được", MB_ICONERROR);
        return false;
    }
    std::wstring cmd = L"\"" + exe + L"\" --acc \"" + x->user + L"\"";
    STARTUPINFOW si = { sizeof(si) }; PROCESS_INFORMATION pi = {};
    if (CreateProcessW(exe.c_str(), &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, ExeDir().c_str(), &si, &pi)) {
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        AddLog(a, L"Mở cửa sổ game cho " + x->user + L" (máy chủ " + (x->sv.empty() ? L"1" : x->sv) + L", " + (x->kenh.empty() ? L"kênh mặc định" : x->kenh) + L")");
        MarkActive(a);
        return true;
    }
    AddLog(a, L"Không mở được vlcmhost.exe (lỗi " + std::to_wstring(GetLastError()) + L")");
    return false;
}

// ------------------------------------------------------------ an / hien cua so game, CPU, hieu nang

struct HostWndCtx { std::wstring pre; HWND h; };
// ham rieng (khong dung lambda): MinGW 32-bit khong doi lambda sang WNDENUMPROC (__stdcall)
static BOOL CALLBACK HostWndEnum(HWND h, LPARAM lp) {
    HostWndCtx* c = (HostWndCtx*)lp;
    wchar_t t[256]; GetWindowTextW(h, t, 256);
    if (_wcsnicmp(t, c->pre.c_str(), c->pre.size()) == 0) { c->h = h; return FALSE; }
    return TRUE;
}
static HWND HostWindow(const Acc* a) {
    HostWndCtx ctx{ L"VLCM Host - " + a->id + L" ", nullptr };
    EnumWindows(HostWndEnum, (LPARAM)&ctx);
    return ctx.h;
}
static void SendCmd(Acc* a, const std::wstring& tag, const std::wstring& line);
static void SetHidden(Acc* a, bool hide) {
    HWND h = HostWindow(a);
    if (!h) { AddLog(a, L"Không tìm thấy cửa sổ game của tài khoản này"); return; }
    if (hide && !a->hidden) {
        GetWindowRect(h, &a->savedRect);
        ShowWindow(h, SW_HIDE);
        SetWindowLongPtrW(h, GWL_EXSTYLE, (GetWindowLongPtrW(h, GWL_EXSTYLE) | WS_EX_TOOLWINDOW) & ~WS_EX_APPWINDOW);
        int w = a->savedRect.right - a->savedRect.left, ht = a->savedRect.bottom - a->savedRect.top;
        SetWindowPos(h, nullptr, GetSystemMetrics(SM_XVIRTUALSCREEN) - w - 200, a->savedRect.top, w, ht, SWP_NOZORDER | SWP_NOACTIVATE);
        ShowWindow(h, SW_SHOWNOACTIVATE);                    // van "hien" (ngoai man hinh) nen Flash khong bi ep cham
        a->hidden = true; a->renderAt = GetTickCount();
        if (a->connected) SendCmd(a, L"render", std::wstring(L"render mode=") + (a->cfg.hideLevel == 2 ? L"hide2" : L"hide1"));
        AddLog(a, std::wstring(L"Đã ẩn cửa sổ game (giảm tải mức ") + (a->cfg.hideLevel == 2 ? L"2" : L"1") + L")");
    } else if (!hide && !a->hidden) {                    // panel mo lai sau khi da an: nhan ra cua so dang nam ngoai man hinh
        RECT r; GetWindowRect(h, &r);
        if (r.right < GetSystemMetrics(SM_XVIRTUALSCREEN) || (GetWindowLongPtrW(h, GWL_EXSTYLE) & WS_EX_TOOLWINDOW)) {
            a->savedRect = { 100, 100, 100 + (r.right - r.left), 100 + (r.bottom - r.top) };
            a->hidden = true;
            SetHidden(a, false);
        }
    } else if (!hide && a->hidden) {
        ShowWindow(h, SW_HIDE);
        SetWindowLongPtrW(h, GWL_EXSTYLE, GetWindowLongPtrW(h, GWL_EXSTYLE) & ~WS_EX_TOOLWINDOW);
        RECT r = a->savedRect;
        SetWindowPos(h, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOZORDER);
        ShowWindow(h, SW_SHOW);
        SetForegroundWindow(h);
        a->hidden = false;
        if (a->connected) SendCmd(a, L"render", L"render mode=show");
        AddLog(a, L"Đã hiện cửa sổ game");
    }
}
/** host cu (chua co lenh quit) hoac khong tra loi: dua cua so ve man hinh roi dong kieu thuong (game co the hoi xac nhan) */
static void CloseFallback(Acc* a) {
    HWND h = HostWindow(a);
    if (!h) return;
    if (a->hidden) SetHidden(a, false);
    PostMessageW(h, WM_CLOSE, 0, 0);
    AddLog(a, L"vlcmhost chưa có lệnh tắt gọn (cần build lại) — đã gửi lệnh đóng cửa sổ thường");
}
static void SetEnabled(Acc* a, bool on);
/** tat client cua tai khoan: bo tick, dung train, gui "quit" (thoat gon, khong hoi) */
static void QuitAcc(Acc* a) {
    if (!a || !a->connected) return;
    a->active = false; a->manualOff = false; a->hostClosing = true; a->relaunchAt = 0; SyncTick(a);
    SetEnabled(a, false);
    if (a->st.state == L"pb") SendCmd(a, L"pbstop", L"pb_stop");
    SendCmd(a, L"quit", L"quit");
    a->quitAt = GetTickCount();
    AddLog(a, L"Tắt cửa sổ game");
}
static int RenderIdx(const std::wstring& r) { return r == L"hide2" ? 2 : r == L"hide1" ? 1 : 0; }
static void SampleCpu(Acc* a) {
    DWORD now = GetTickCount();
    if (!a->connected) { a->cpu = -1; a->pid = 0; return; }
    if (!a->pid) { HWND h = HostWindow(a); if (h) GetWindowThreadProcessId(h, &a->pid); a->lastCpu = 0; }
    if (!a->pid) return;
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, a->pid);
    if (!p) { a->pid = 0; return; }
    FILETIME c, e, k, u;
    if (GetProcessTimes(p, &c, &e, &k, &u)) {
        ULONGLONG t = (((ULONGLONG)k.dwHighDateTime << 32) | k.dwLowDateTime) + (((ULONGLONG)u.dwHighDateTime << 32) | u.dwLowDateTime);
        if (a->lastCpu && now > a->lastCpuAt) {
            SYSTEM_INFO si; GetSystemInfo(&si);
            double cpuMs = (t - a->lastCpu) / 10000.0, wall = (double)(now - a->lastCpuAt);
            a->cpu = 100.0 * cpuMs / wall / (std::max)(1u, (unsigned)si.dwNumberOfProcessors);
            int i = RenderIdx(a->st.render);
            a->cpuMs[i] += cpuMs / (std::max)(1u, (unsigned)si.dwNumberOfProcessors); a->cpuWall[i] += wall;
        }
        a->lastCpu = t; a->lastCpuAt = now;
    }
    CloseHandle(p);
}
static std::wstring Fmt1(double v) { wchar_t b[32]; swprintf(b, 32, L"%.1f", v); return b; }
static void RefreshPerf() {
    Acc* a = Sel();
    std::vector<std::vector<double>> B(3);
    if (a) {
        size_t i = 0; std::wstring p = a->st.perf; int k = 0;
        while (k < 3 && i <= p.size()) {
            size_t e = p.find(L'/', i); if (e == std::wstring::npos) e = p.size();
            std::wstring one = p.substr(i, e - i); size_t j = 0;
            while (j <= one.size()) { size_t f = one.find(L',', j); if (f == std::wstring::npos) f = one.size(); B[k].push_back(_wtof(one.substr(j, f - j).c_str())); j = f + 1; }
            ++k; i = e + 1;
        }
    }
    for (int c = 0; c < 3; ++c) {
        auto& b = B[c];
        auto at = [&](int n) { return n < (int)b.size() ? b[n] : 0.0; };
        double ms = at(0), min = ms / 60000.0;
        bool has = ms > 5000;
        LvText(g_perf, 0, c + 1, has ? std::to_wstring((int)(ms / 60000)) + L" phút " + std::to_wstring(((int)ms / 1000) % 60) + L" giây" : L"-");
        LvText(g_perf, 1, c + 1, a && a->cpuWall[c] > 5000 ? Fmt1(100.0 * a->cpuMs[c] / a->cpuWall[c]) + L"%" : L"-");
        LvText(g_perf, 2, c + 1, has ? Fmt1(at(1) * 1000.0 / ms) + L" / " + std::to_wstring((int)at(2)) : L"-");
        LvText(g_perf, 3, c + 1, has && at(4) > 0 ? Fmt1(at(3) / at(4)) + L" / " + std::to_wstring((int)at(5)) + L" ms" : L"-");
        LvText(g_perf, 4, c + 1, has ? Fmt1(at(6) / min) : L"-");
        LvText(g_perf, 5, c + 1, has ? Fmt1(at(7) / min) : L"-");
        LvText(g_perf, 6, c + 1, has && at(9) > 0 ? Fmt1(at(8) / at(9) / 1000.0) + L" giây" : L"-");
        LvText(g_perf, 7, c + 1, has && at(11) > 0 ? Fmt1(at(10) / 10.0 / (at(11) / 1000.0)) : L"-");
    }
}

// ------------------------------------------------------------ gui lenh

static void SendCmd(Acc* a, const std::wstring& tag, const std::wstring& line) {
    if (!a->link || !a->connected) return;
    a->pending.push_back({ tag, GetTickCount() });
    a->link->Send(line);
}

static std::vector<size_t> ActivePoints(const Acc* a) {
    std::vector<size_t> v;
    for (size_t i = 0; i < a->cfg.pts.size(); ++i) if (a->cfg.pts[i].on) v.push_back(i);
    return v;
}

// Cai dat tien ich: gui rieng, khong can khoi dong lai train
static void SendUtil(Acc* a) {
    if (!a->connected) return;
    const Config& c = a->cfg;
    std::wstring hpids, mpids;
    for (int i = 0; i < NDRUGS; ++i) {
        std::wstring id = std::to_wstring(DRUGS[i].id);
        if (!HasId(c.drugIds, id)) continue;
        std::wstring& dst = DRUGS[i].hp ? hpids : mpids;
        dst += (dst.empty() ? L"" : L",") + id;
    }
    std::wstring rules;
    for (auto& r : c.rules) if (RuleValid(r)) rules += (rules.empty() ? L"" : L"\n") + RuleLine(r);
    std::wstring cmd = L"util_set pot=" + std::wstring(c.potMode == 1 ? L"items" : L"keys")
        + L" hpkey=" + (c.hpkey.empty() ? L"-1" : std::to_wstring(ToInt(c.hpkey, -1)))
        + L" mpkey=" + (c.mpkey.empty() ? L"-1" : std::to_wstring(ToInt(c.mpkey, -1)))
        + L" hp=" + std::to_wstring(c.hp) + L" mp=" + std::to_wstring(c.mp)
        + L" hpids=" + hpids + L" mpids=" + mpids
        + L" repair=" + B(c.repairOn) + L" rtype=" + std::to_wstring(c.repairType)
        + L" bh=" + B(c.bhOn) + L" floorq=" + (c.floorQ ? L"4" : L"99") + L" floors=" + (c.floorS > 0 ? std::to_wstring(c.floorS) : L"99")
        + L" freee=" + std::to_wstring(c.freeE) + L" freei=" + std::to_wstring(c.freeI)
        + L" tele=" + B(c.tele) + L" vllauto=" + B(c.vllAuto)
        + L" gs=" + [&]() { std::wstring g; for (int i = 0; i < 13; ++i) g += (i ? L"," : L"") + std::to_wstring(i) + L":" + (c.gs[i] ? L"1" : L"0"); return g; }()
        + L" hpfx=" + B(c.hpfx) + L" closeui=" + B(c.closeUi)
        + L" protect=" + Encode(c.protect) + L" rules=" + Encode(rules);
    SendCmd(a, L"util", cmd);
    a->utilSent = true;
}

/** ky nang (bo dang chon) + ho tro + nhat do: gui kem train_start va pb_start */
static std::wstring FightArgs(const Config& c) {
    static const wchar_t* picks[] = { L"off", L"list", L"all" };
    const Config::SkSet& ss = c.sets[(std::min)(4, (std::max)(0, c.trainSet))];
    std::wstring heal;                       // "id:pct,..."
    for (wchar_t ch : ss.heal) if ((ch >= L'0' && ch <= L'9') || ch == L',' || ch == L':') heal += ch;
    return std::wstring(L" skillmode=") + (c.skillMode == 1 ? L"keys" : L"sets")
        + L" g1=" + NumList(ss.g[0]) + L" g2=" + NumList(ss.g[1]) + L" g3=" + NumList(ss.g[2]) + L" g4=" + NumList(ss.g[3])
        + L" heal=" + heal + L" buff=" + NumList(ss.buff) + L" supmp=" + std::to_wstring(ss.supmp) + L" keys=" + NumList(c.keys)
        + L" pick=" + picks[(std::min)(2, (std::max)(0, c.pickMode))] + L" picklist=" + Encode(c.pickList);
}
static void StartTrain(Acc* a, const wchar_t* why) {
    auto act = ActivePoints(a);
    if (act.empty()) {                                   // bao mot lan cho toi khi co toa do duoc tick
        if (!a->noPtsLogged) AddLog(a, L"Đánh quái - Chưa có tọa độ nào được tick (tab TRAIN > TỌA ĐỘ)");
        a->noPtsLogged = true; a->lastStart = GetTickCount(); return;
    }
    a->noPtsLogged = false;
    if (a->curPt >= act.size()) a->curPt = 0;
    const TrainPoint& p = a->cfg.pts[act[a->curPt]];
    const Config& c = a->cfg;
    std::wstring types = std::wstring(c.tNormal ? L"1," : L"") + (c.tElite ? L"2," : L"") + (c.tBoss ? L"3," : L"");
    if (types.empty()) types = L"1,";
    types.pop_back();
    std::wstring cmd = L"train_start map=" + std::to_wstring(p.map) + L" x=" + std::to_wstring(p.x) + L" y=" + std::to_wstring(p.y)
        + L" r=" + std::to_wstring(p.r) + FightArgs(c)
        + L" types=" + types + L" restdeaths=" + std::to_wstring(c.restOn ? c.restDeaths : 0) + L" restmin=" + std::to_wstring(c.restMin);
    if (!a->utilSent) SendUtil(a);
    SendCmd(a, L"start", cmd);
    a->lastStart = GetTickCount();
    if (why) AddLog(a, std::wstring(L"Đánh quái - ") + why + L": " + (p.name.empty() ? L"Map " + std::to_wstring(p.map) : p.name) +
                        L" " + std::to_wstring(p.x) + L":" + std::to_wstring(p.y));
}

static void NextPoint(Acc* a) {
    auto act = ActivePoints(a);
    if (act.size() < 2) return;
    a->curPt = (a->curPt + 1) % act.size();
    a->ptSince = GetTickCount();
}

static void SetEnabled(Acc* a, bool on) {
    if (a->cfg.enabled == on) return;
    a->cfg.enabled = on;
    SaveConfig(*a);
    if (on) {
        a->ptSince = GetTickCount();
        if (a->active && a->connected && a->st.inGame && !PbWanted(a) && a->st.state != L"pb") StartTrain(a, L"Bắt đầu");
        else AddLog(a, L"Đánh quái - Đang chờ (tài khoản chưa tick hoặc chưa vào game)");
    } else {
        if (a->connected) SendCmd(a, L"stop", L"train_stop");
        AddLog(a, L"Đánh quái - Dừng");
    }
    if (a == Sel()) RefreshTrainState();
}

// ------------------------------------------------------------ xu ly dong tu vlcmhost

static std::vector<std::wstring> SplitComma(const std::wstring& list) {
    std::vector<std::wstring> v; size_t i = 0;
    while (i < list.size()) { size_t e = list.find(L',', i); if (e == std::wstring::npos) e = list.size(); v.push_back(list.substr(i, e - i)); i = e + 1; }
    return v;
}

static void OnReply(Acc* a, const std::wstring& tag, const std::wstring& r) {
    bool ok = StartsWith(r, L"ok");
    if (tag == L"status") {
        bool was = a->st.inGame;
        if (StartsWith(r, L"err")) { a->st.inGame = false; }
        else {
            auto kv = ParseKV(r);
            Status& s = a->st;
            s.inGame = kv.count(L"map") > 0;
            s.state = kv[L"state"];
            s.name = kv[L"name"]; s.mapname = kv[L"mapname"];
            s.map = ToInt(kv[L"map"]); s.x = ToInt(kv[L"x"]); s.y = ToInt(kv[L"y"]);
            std::wstring hp = kv[L"hp"]; size_t sl = hp.find(L'/');
            s.hp = ToInt(hp.substr(0, sl)); s.hpmax = sl == std::wstring::npos ? 0 : ToInt(hp.substr(sl + 1));
            s.lv = ToInt(kv[L"lv"]); s.kills = ToInt(kv[L"kills"]); s.deaths = ToInt(kv[L"deaths"]); s.picked = ToInt(kv[L"picked"]);
            s.rest = kv.count(L"rest") ? ToInt(kv[L"rest"]) : -1;
            s.vip = kv[L"vip"] == L"1"; s.vipexp = _wtoi64(kv[L"vipexp"].c_str());
            s.copper = _wtoi64(kv[L"copper"].c_str()); s.mpick = _wtoi64(kv[L"mpick"].c_str());
            s.msold = _wtoi64(kv[L"msold"].c_str()); s.mrep = _wtoi64(kv[L"mrep"].c_str()); s.since = _wtoi64(kv[L"since"].c_str());
            s.free = kv.count(L"free") ? ToInt(kv[L"free"]) : -1; s.dur = kv.count(L"dur") ? ToInt(kv[L"dur"]) : -1;
            s.bh = kv[L"bh"]; s.bhn = kv[L"bhn"]; s.trip = kv[L"trip"];
            s.render = kv[L"render"]; s.perf = kv[L"perf"];
            s.mlost = ToInt(kv[L"mlost"]); s.prefused = ToInt(kv[L"prefused"]);
            s.upause = kv[L"upause"];
            s.pb = kv[L"pb"]; s.pbphase = kv[L"pbphase"]; s.pbfloor = kv[L"pbfloor"];
            s.roses = kv.count(L"roses") ? ToInt(kv[L"roses"]) : -1;
            s.lzmax = kv.count(L"lzmax") ? ToInt(kv[L"lzmax"]) : -1; s.lzbrk = ToInt(kv[L"lzbrk"]); s.lzresc = ToInt(kv[L"lzresc"]); s.lzgap = ToInt(kv[L"lzgap"]);
            s.lz = ToInt(kv[L"lz"]); s.lzb = kv.count(L"lzb") ? ToInt(kv[L"lzb"]) : -2; s.lzbmin = ToInt(kv[L"lzbmin"]);
        }
        if (a->st.inGame && !a->st.name.empty()) {             // tu dien ten nhan vat vao accounts.xml
            auto v = LoadXmlAccounts();
            if (const XmlAcc* x = FindXml(v, a->id)) {
                a->savedNv = a->st.name;
                if (x->nv != a->st.name) {
                    XmlAcc y = *x; y.nv = a->st.name;
                    if (SaveXmlAccount(y)) {
                        a->savedNv = y.nv;
                        AddLog(a, L"Đã lưu nhân vật \"" + y.nv + L"\" cho tài khoản (lần sau tự chọn)");
                        if (a == Sel() && !g_newAcc) SetText(IDC_EDIT_A_NV, y.nv);
                    }
                }
            }
        }
        if (a->st.inGame && !a->adopted) {                     // panel mo lai khi train dang chay san: tick lai o Danh quai
            a->adopted = true;
            int pi = PbIndex(a->st.pb);
            if (a->st.state == L"pb" && pi >= 0 && !a->cfg.pbOn[pi]) {
                a->cfg.pbOn[pi] = true;
                AddLog(a, std::wstring(L"Phó bản ") + PB_NAME[pi] + L" đang chạy sẵn — đã tick lại");
            } else if (!a->cfg.enabled && a->active && !a->st.state.empty() && a->st.state != L"idle" && a->st.state != L"pb") {
                a->cfg.enabled = true; SaveConfig(*a);
                AddLog(a, L"Đánh quái đang chạy sẵn — đã tick lại");
            }
        }
        if (a->st.inGame) a->gameDiscoAt = 0;
        if (!a->st.inGame) a->utilSent = false;
        else if (!was || !a->utilSent) SendUtil(a);            // vao game (hoac host nap lai SWF): gui lai cai dat tien ich
        RefreshRow(a);
        if (a == Sel()) RefreshTrainState();
    } else if (tag == L"where") {
        if (!ok) { AddLog(a, L"Thêm tọa độ lỗi: " + r); return; }
        auto kv = ParseKV(r.substr(3));
        TrainPoint p;
        p.map = ToInt(kv[L"map"]); p.x = ToInt(kv[L"x"]); p.y = ToInt(kv[L"y"]); p.name = kv[L"name"];
        p.r = (std::max)(1, ToInt(a == Sel() ? GetText(IDC_EDIT_R) : L"5", 5));
        a->cfg.pts.push_back(p);
        SaveConfig(*a);
        if (a == Sel()) RefreshPoints();
        AddLog(a, L"Thêm tọa độ " + (p.name.empty() ? L"Map " + std::to_wstring(p.map) : p.name) + L" {" +
                  std::to_wstring(p.x) + L":" + std::to_wstring(p.y) + L"} phạm vi " + std::to_wstring(p.r));
    } else if (tag == L"skills") {
        if (!ok) { AddLog(a, L"Lấy skill lỗi: " + r); return; }
        std::vector<SkillRow> rows;
        for (auto& one : SplitComma(r.size() > 3 ? r.substr(3) : L"")) {
            std::vector<std::wstring> f;
            size_t i = 0;
            while (i <= one.size()) { size_t e = one.find(L':', i); if (e == std::wstring::npos) e = one.size(); f.push_back(one.substr(i, e - i)); i = e + 1; }
            if (f.size() < 3) continue;
            std::wstring useway = f.size() > 3 ? f[3] : L"1";
            if (useway != L"1") continue;
            rows.push_back({ f[0], Decode(f[1]), f[2], SkillGroup(f[0]), f.size() > 4 ? f[4] : L"", f.size() > 5 ? f[5] : L"" });
        }
        std::stable_sort(rows.begin(), rows.end(), [](const SkillRow& x, const SkillRow& y) {
            auto rank = [](const std::wstring& g) { return g == L"Môn phái" ? 0 : g == L"Giang hồ" ? 1 : g == L"Bang phái" ? 2 : 3; };
            return rank(x.group) < rank(y.group);
        });
        g_skills[a->id] = rows;
        DefaultSupport(a); if (a == Sel()) RefreshSkillPage();
        AddLog(a, L"Đã lấy " + std::to_wstring(rows.size()) + L" skill chủ động");
    } else if (tag == L"bag") {
        if (!ok) { AddLog(a, L"Lấy tên đồ lỗi: " + r); return; }
        if (a != Sel()) return;
        HWND lb = Item(IDC_BAGLIST);
        SendMessageW(lb, LB_RESETCONTENT, 0, 0);
        for (auto& e : SplitComma(r.size() > 3 ? r.substr(3) : L"")) {
            std::wstring n = Decode(e);
            if (!n.empty()) SendMessageW(lb, LB_ADDSTRING, 0, (LPARAM)n.c_str());
        }
    } else if (tag == L"dump") {
        if (!ok) { AddLog(a, L"Xem túi lỗi: " + r); return; }
        std::wstring f = DataDir() + L"\\tui_" + a->id + L".txt";
        DeleteFileW(f.c_str());
        AppendFile(f, L"Túi đồ " + a->id + L" lúc " + Today() + L"  (o=ô, q=phẩm chất 0-1 Trắng 2 Lam 3 Lục 4 Tím 5-6 Vàng, "
                      L"sao=cường hóa, khoa=1 đồ khóa, kind=2 trang bị, cap=cấp trang bị, capdung=cấp sử dụng, phai=môn phái, vitri=loại/ô trang bị)");
        int n = 0;
        for (auto& e : SplitComma(r.size() > 3 ? r.substr(3) : L"")) { AppendFile(f, Decode(e)); ++n; }
        AddLog(a, L"Đã ghi " + std::to_wstring(n) + L" món vào " + f);
        ShellExecuteW(nullptr, L"open", f.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    } else if (tag == L"quit") {
        a->quitAt = 0;                                          // host da nhan lenh (ok) hoac khong biet lenh (host cu)
        if (!ok) CloseFallback(a);
    } else if (tag == L"pbstart") {
        if (!ok) AddLog(a, L"Phó bản - Lỗi: " + r);
    } else if (tag == L"pblist") {
        if (!ok && r.find(L"không hỗ trợ") != std::wstring::npos) {   // VlcmLoader.swf ban cu: dung roi chay lai voi danh sach moi
            AddLog(a, L"Phó bản - VlcmLoader.swf chưa có lệnh pb_list (cần bản mới) — dừng rồi chạy lại với danh sách mới");
            SendCmd(a, L"pbstop", L"pb_stop");
            a->pbStartAt = GetTickCount() - 7000;
        } else if (!ok) AddLog(a, L"Phó bản - Lỗi: " + r);
    } else if (tag == L"pause") {
        if (!ok) AddLog(a, L"Tạm dừng tiện ích lỗi: " + r);
    } else if (tag == L"start" || tag == L"stop") {
        if (!ok) AddLog(a, L"Đánh quái - Lỗi: " + r);
    } else if (tag == L"util") {
        if (!ok) { AddLog(a, L"Tiện ích - Lỗi: " + r); a->utilSent = false; }
    }
}

static std::wstring AuditText(const std::wstring& d) {        // "SELL name=.. id=.." -> dong de doc
    size_t sp = d.find(L' ');
    std::wstring tag = d.substr(0, sp);
    auto kv = ParseKV(sp == std::wstring::npos ? L"" : d.substr(sp + 1));
    std::wstring what = tag == L"SELL" ? L"Đã bán" : tag == L"DESTROY" ? L"Đã hủy" : tag == L"DRY-SELL" ? L"Chạy thử - sẽ bán" : L"Chạy thử - sẽ hủy";
    std::wstring t = what + L" " + kv[L"name"] + L" x" + kv[L"sl"] + L" (ô " + kv[L"o"] + L", quy tắc " + kv[L"rule"] + L")";
    if (!kv[L"money"].empty()) t += L" +" + kv[L"money"] + L" đồng";
    return t;
}

/** su kien "pb ..." tu SWF */
static void PbEvent(Acc* a, const std::wstring& d) {
    size_t sp = d.find(L' ');
    std::wstring cmd = d.substr(0, sp), rest = sp == std::wstring::npos ? L"" : d.substr(sp + 1);
    size_t s2 = rest.find(L' ');
    std::wstring key = rest.substr(0, s2), tail = s2 == std::wstring::npos ? L"" : rest.substr(s2 + 1);
    int i = PbIndex(key);
    std::wstring nm = i >= 0 ? std::wstring(L"Phó bản ") + PB_NAME[i] + L" - " : L"Phó bản - ";
    if (cmd == L"end" && i >= 0) {
        bool ok = StartsWith(tail, L"ok");
        std::wstring why = tail.size() > 3 ? tail.substr(tail.find(L' ') == std::wstring::npos ? tail.size() : tail.find(L' ') + 1) : L"";
        if (ok) { PbDayCheck(a); a->cfg.pbDone[i]++; SaveConfig(*a); }
        if (ok && a->cfg.npcEnter[i] >= 0) { a->cfg.npcEnter[i]++; SaveConfig(*a); }
        AddLog(a, nm + (ok ? L"Hoàn thành lượt (hôm nay " + (a->cfg.npcTotal[i] >= 0 ? std::to_wstring(a->cfg.npcEnter[i]) + L"/" + std::to_wstring(a->cfg.npcTotal[i]) : std::to_wstring(a->cfg.pbDone[i])) + L")"
                           : L"Lượt thất bại: " + why));
    } else if (cmd == L"skip" && i >= 0) {
        a->pbSkip[i] = tail;
        AddLog(a, nm + L"Bỏ qua: " + tail);
    } else if (cmd == L"count" && i >= 0) {
        int e = -1, t = -1;
        if (swscanf(tail.c_str(), L"%d %d", &e, &t) == 2) {
            PbDayCheck(a);
            bool ch = a->cfg.npcEnter[i] != e || a->cfg.npcTotal[i] != t;
            a->cfg.npcEnter[i] = e; a->cfg.npcTotal[i] = t; SaveConfig(*a);
            if (ch) AddLog(a, nm + L"Số lượt theo NPC: đã đi " + std::to_wstring(e) + L"/" + std::to_wstring(t));
        }
    } else if (cmd == L"wait" && i >= 0) {
        a->pbWaitAt[i] = GetTickCount();
        AddLog(a, nm + L"Đang chờ: " + tail);
    } else if (cmd == L"enter") AddLog(a, nm + L"Vào phó bản " + tail);
    else if (cmd == L"run") AddLog(a, nm + L"Bắt đầu lượt " + tail);
    else if (cmd == L"floor") { size_t m = tail.find(L" map"); AddLog(a, nm + L"Tầng " + (m == std::wstring::npos ? tail : tail.substr(0, m))); }
    else if (cmd == L"reward") AddLog(a, nm + L"Nhận thưởng");
    else if (cmd == L"exit") AddLog(a, nm + L"Thoát: " + tail);
    else if (cmd == L"revive") AddLog(a, L"Phó bản - Hồi sinh " + rest);
    else if (cmd == L"confirm") AddLog(a, L"Phó bản - Đã xác nhận: " + rest);
    else if (cmd == L"finish") { AddLog(a, L"Phó bản - Đã đi hết các phó bản đã chọn"); a->st.state = L"idle"; a->pbStartAt = GetTickCount(); }
    else if (cmd == L"start") AddLog(a, L"Phó bản - Bắt đầu: " + rest);
    else if (cmd == L"list") AddLog(a, L"Phó bản - Cập nhật danh sách: " + rest);
    else if (cmd == L"unpick") AddLog(a, nm + L"Đã bỏ tick phó bản đang làm — thoát phó bản này");
    else if (cmd == L"stop") AddLog(a, L"Phó bản - Dừng: " + rest);
    else AddLog(a, L"Phó bản - " + d);
    if (a == Sel()) RefreshTrainState();
}
static void OnEvent(Acc* a, const std::wstring& ev) {
    size_t sp = ev.find(L' ');
    std::wstring name = ev.substr(0, sp), d = sp == std::wstring::npos ? L"" : ev.substr(sp + 1);
    if (name == L"hostclose") { a->hostClosing = true; return; }       // vlcmhost dong theo y nguoi dung: khong tu mo lai
    if (name == L"train" && StartsWith(d, L"pb ")) { PbEvent(a, d.substr(3)); return; }
    if (name == L"train") {
        if (StartsWith(d, L"status ")) return;                         // panel tu hoi status
        if (StartsWith(d, L"state ")) {
            std::wstring rest = d.substr(6);
            size_t s2 = rest.find(L' ');
            std::wstring st = rest.substr(0, s2), why = s2 == std::wstring::npos ? L"" : rest.substr(s2 + 1);
            AddLog(a, (st == L"town" ? L"Về thành - " : L"Đánh quái - ") + why);
            a->st.state = st;
            RefreshRow(a);
            if (a == Sel()) RefreshTrainState();
            if (st == L"dead" && a->cfg.enabled && a->active && a->cfg.swDeath && ActivePoints(a).size() > 1) {
                NextPoint(a);
                StartTrain(a, L"Chuyển bãi (bị giết)");
            }
        } else if (StartsWith(d, L"revive ")) AddLog(a, L"Đánh quái - Hồi sinh về thành " + d.substr(7));
        else if (StartsWith(d, L"error "))   AddLog(a, L"Đánh quái - Lỗi: " + d.substr(6));
        else if (StartsWith(d, L"stop "))    AddLog(a, L"Đánh quái - Đã dừng (" + d.substr(5) + L")");
        else if (StartsWith(d, L"info "))    AddLog(a, L"Đánh quái - " + d.substr(5));
        else if (StartsWith(d, L"warn "))    AddLog(a, L"Đánh quái - Cảnh báo: " + d.substr(5));
        else if (StartsWith(d, L"pick ok ")) AddLog(a, L"Nhặt đồ - Đã nhặt " + d.substr(8));
        else if (StartsWith(d, L"pick lost ")) AddLog(a, L"Nhặt đồ - Mất: " + d.substr(10));
        else if (StartsWith(d, L"pick skip ")) AddLog(a, L"Nhặt đồ - Bỏ qua: " + d.substr(10));
        return;                                                        // "start ..." da log khi gui
    }
    if (name == L"util") {
        // "util info|warn|error ..." hoac "bh audit|stop|warn|info ..."
        size_t s2 = d.find(L' ');
        std::wstring grp = d.substr(0, s2), rest = s2 == std::wstring::npos ? L"" : d.substr(s2 + 1);
        size_t s3 = rest.find(L' ');
        std::wstring kind = rest.substr(0, s3), msg = s3 == std::wstring::npos ? L"" : rest.substr(s3 + 1);
        if (grp == L"bh") {
            if (kind == L"audit") {
                std::wstring t = AuditText(msg);
                AddLog(a, L"Bán/Hủy - " + t);
                AppendFile(DataDir() + L"\\banhuy_" + a->id + L".log", Today() + L"  " + t + L"  | " + msg);
            } else if (kind == L"stop") {
                AddLog(a, L"Bán/Hủy - ĐÃ DỪNG KHẨN CẤP: " + msg + L" (bấm \"Bật lại\" sau khi kiểm tra)");
                AppendFile(DataDir() + L"\\banhuy_" + a->id + L".log", Today() + L"  DỪNG: " + msg);
                MessageBeep(MB_ICONWARNING);
            } else AddLog(a, std::wstring(L"Bán/Hủy - ") + (kind == L"warn" ? L"Cảnh báo: " : L"") + msg);
        } else {
            AddLog(a, std::wstring(L"Tiện ích - ") + (kind == L"warn" ? L"Cảnh báo: " : kind == L"error" ? L"Lỗi: " : L"") + msg);
        }
        return;
    }
    if (name == L"entered_game") AddLog(a, L"Đã vào game");
    else if (name == L"disconnected") { AddLog(a, L"Mất kết nối game"); a->gameDiscoAt = GetTickCount(); }
    else if (name == L"error") AddLog(a, L"Loader lỗi: " + d);
    else if (name == L"line_not_found") AddLog(a, L"Không có kênh cần vào, chọn tay");
    else if (name == L"char_not_found") AddLog(a, L"Không thấy nhân vật, chọn tay");
}

static void OnLine(Acc* a, const std::wstring& line) {
    if (StartsWith(line, L"> ")) {
        std::wstring r = line.substr(2), tag;
        if (!a->pending.empty()) { tag = a->pending.front().tag; a->pending.pop_front(); }
        // Kiem tra hinh dang reply: status tra "state=..."/"err ...", lenh khac tra "ok ..."/"err ...".
        // Lech hinh dang => da mat mot reply o dau do: xoa hang cho de dong bo lai.
        bool looksStatus = StartsWith(r, L"state=");
        if (!tag.empty() && (tag == L"status") != looksStatus && !StartsWith(r, L"err")) {
            a->pending.clear();
            tag = looksStatus ? L"status" : L"";
        }
        OnReply(a, tag, r);
    } else if (StartsWith(line, L"! ")) {
        OnEvent(a, line.substr(2));
    }
}

// ------------------------------------------------------------ tim cua so vlcmhost

static void AddAcc(const std::wstring& id) {
    if (id.empty() || Find(id)) return;
    auto a = std::make_unique<Acc>();
    a->id = id;
    LoadConfig(*a);
    a->cfg.enabled = false;                  // mo panel: khong tu bat Danh quai (train dang chay san se duoc tick lai khi noi)
    a->link = std::make_unique<Link>(g_hwnd, id);
    g_syncTick = true;
    int row = LvInsert(g_acc, (int)g_accs.size(), id);
    ListView_SetCheckState(g_acc, row, FALSE);
    g_syncTick = false;
    g_accs.push_back(std::move(a));
    RefreshRow(g_accs.back().get());
    if (g_sel < 0) ListView_SetItemState(g_acc, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    RefreshConnInfo();
}

static void LoadXmlRows() {
    for (auto& x : LoadXmlAccounts()) {
        bool have = false;
        for (auto& p : g_accs) if (_wcsicmp(p->id.c_str(), x.user.c_str()) == 0) { p->inXml = true; p->savedNv = x.nv; have = true; }
        if (!have) { AddAcc(x.user); for (auto& p : g_accs) if (p->id == x.user) { p->inXml = true; p->savedNv = x.nv; RefreshRow(p.get()); } }
    }
}

static void Discover() {
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW(L"\\\\.\\pipe\\*", &fd);
    if (f != INVALID_HANDLE_VALUE) {
        do {
            std::wstring n = fd.cFileName;
            if (StartsWith(n, L"vlcmhost-")) {
                std::wstring id = n.substr(9);
                bool have = false;
                for (auto& p : g_accs) if (_wcsicmp(p->id.c_str(), id.c_str()) == 0) have = true;
                if (!have) AddAcc(id);
            }
        } while (FindNextFileW(f, &fd));
        FindClose(f);
    }
    for (auto& h : g_argHosts) AddAcc(h);
}

// ------------------------------------------------------------ vong kiem tra moi giay

static void Tick() {
    DWORD now = GetTickCount();
    for (auto& up : g_accs) {
        Acc* a = up.get();
        if (!a->connected) continue;
        // Reply nao cho qua 8s coi nhu mat (pipe truc trac / Flash treo): xoa ca hang cho de khong lech tag vinh vien.
        if (!a->pending.empty() && now - a->pending.front().at > 8000) a->pending.clear();
        bool waitingStatus = std::any_of(a->pending.begin(), a->pending.end(), [](const Acc::PendingCmd& p) { return p.tag == L"status"; });
        if (now - a->lastStatus >= 2000 && !waitingStatus) { SendCmd(a, L"status", L"status"); a->lastStatus = now; }
        if (a->quitAt && now - a->quitAt > 4000) { a->quitAt = 0; CloseFallback(a); continue; }
        if (a->st.inGame && (a->st.upause == L"1") != !a->active && now - a->pauseAt > 5000) {   // dong bo tam dung tien ich (ca khi host nap lai SWF)
            a->pauseAt = now;
            SendCmd(a, L"pause", std::wstring(L"util_pause on=") + (a->active ? L"0" : L"1"));
        }
        PbDayCheck(a);
        if (a->active && a->st.inGame) {
            bool want = PbWanted(a);
            if (want && a->st.state != L"pb" && now - a->pbStartAt > 10000) { a->pbStartAt = now; SendCmd(a, L"pbstart", L"pb_start " + PbArgs(a)); }
            // khong tick pho ban nao: SWF tu thoat pho ban dang lam (pb_list) roi bao "pb finish", khong dung ngang
            else if (!want && a->st.state == L"pb" && now - a->pbStartAt > 10000 && PbAnyOn(a)) { a->pbStartAt = now; SendCmd(a, L"pbstop", L"pb_stop"); }
        }
        // game bao mat ket noi ma 90s chua vao lai: dang nhap lai trong cua so dang mo
        if (a->gameDiscoAt && !a->st.inGame && now - a->gameDiscoAt > 90000 && a->active) {
            a->gameDiscoAt = 0;
            AddLog(a, L"Mất kết nối game quá 90 giây — đăng nhập lại");
            LoginAcc(a);
            continue;
        }
        if (!a->cfg.enabled || !a->active || !a->st.inGame || PbWanted(a) || a->st.state == L"pb") continue;
        if (a->restartAt && now >= a->restartAt) { a->restartAt = 0; StartTrain(a, L"Áp dụng cài đặt mới"); continue; }
        if (a->st.state == L"idle" && now - a->lastStart > 6000) { StartTrain(a, L"Bắt đầu"); continue; }
        if (a->cfg.swTimeOn && ActivePoints(a).size() > 1 && now - a->ptSince >= (DWORD)a->cfg.swMin * 60000u &&
            a->st.state != L"dead" && a->st.state != L"resting" && a->st.state != L"town") {
            NextPoint(a);
            StartTrain(a, L"Chuyển bãi (hết giờ)");
        }
    }
    for (auto& up : g_accs) {
        Acc* a = up.get();
        if (!a->connected && a->relaunchAt && now >= a->relaunchAt) {     // client tat bat thuong: tu mo lai
            a->relaunchAt = 0;
            a->relaunches.push_back(now);
            AddLog(a, L"Tự mở lại client");
            if (!LoginAcc(a)) { a->active = false; SyncTick(a); }
        }
        SampleCpu(a);
        RefreshRow(a);
        if (a->connected && a->st.inGame && !a->skillsAsked) { a->skillsAsked = true; SendCmd(a, L"skills", L"skills"); }
        if (a->connected) {
            HWND h = HostWindow(a);
            bool off = h && (GetWindowLongPtrW(h, GWL_EXSTYLE) & WS_EX_TOOLWINDOW);
            if (h && a->hidden && !off) { a->hidden = false; a->pid = 0; SetHidden(a, true); }   // host mo lai trong luc an: an lai cua so moi
            else if (off && !a->hidden) {                // panel mo lai khi cua so van dang an
                RECT r; GetWindowRect(h, &r);
                a->savedRect = { 100, 100, 100 + (r.right - r.left), 100 + (r.bottom - r.top) };
                a->hidden = true;
            }
        }
        if (a->connected && a->st.inGame && a->hidden && a->st.render == L"show" && GetTickCount() - a->renderAt > 5000) {  // host nap lai SWF
            a->renderAt = GetTickCount();
            SendCmd(a, L"render", std::wstring(L"render mode=") + (a->cfg.hideLevel == 2 ? L"hide2" : L"hide1"));
        }
    }
    if (Sel()) { RefreshStats(); if (TabCtrl_GetCurSel(g_tabMain) == 4) RefreshPerf(); }
}

// ------------------------------------------------------------ luu cai dat khi sua

static void ConfigChanged(bool trainPart = true) {
    if (g_loading) return;
    Acc* a = Sel();
    if (!a) return;
    ReadUI(a->cfg);
    SaveConfig(*a);
    if (!trainPart) { if (a->connected && a->st.inGame) SendUtil(a); return; }
    if (a->cfg.enabled && a->connected && a->st.inGame) a->restartAt = GetTickCount() + 1500;   // gom nhieu lan sua
}
static void UtilChanged() { ConfigChanged(false); }

// ------------------------------------------------------------ bo cuc

static void Layout() {
    RECT rc; GetClientRect(g_hwnd, &rc);
    int W = rc.right, H = rc.bottom, m = 8;
    MoveWindow(g_acc, m, m, W - 2 * m, 170, TRUE);
    int y = m + 170 + 6;
    Move(IDC_CONNINFO, m + 4, y + 5, W - 12 - m, 20);
    int top = y + 36, leftW = 330;
    Move(IDC_LBL_CTRL, m, top, leftW, 22);
    for (int i = 0; i < NPB; ++i) {
        int ry = top + 30 + i * 28;
        Move(IDC_CHK_PB0 + i, m + 4, ry, 170, 24); Move(IDC_PB_ST0 + i, m + 180, ry + 1, 90, 22); Move(IDC_BTN_PBG0 + i, m + 276, ry, 30, 24);
    }
    int pbh = NPB * 28;
    Move(IDC_CHK_TRAIN, m + 4, top + 30 + pbh, 170, 24);
    Move(IDC_TRAIN_STATE, m + 180, top + 31 + pbh, 90, 22);
    Move(IDC_BTN_GEAR, m + 276, top + 30 + pbh, 30, 24);
    int ly = top + 66 + pbh;
    Move(IDC_LBL_VIP, m + 4, ly, 240, 22); ly += 26;
    Move(IDC_LBL_STAT1, m + 4, ly, leftW - 8, 20); ly += 22;
    Move(IDC_LBL_STAT2, m + 4, ly, leftW - 8, 20); ly += 22;
    Move(IDC_LBL_STAT3, m + 4, ly, leftW - 8, 20); ly += 22;
    Move(IDC_BTN_STATRESET, m + 4, ly, 150, 24); ly += 34;
    Move(IDC_LBL_BHSTATE, m + 4, ly, leftW - 8, 22); ly += 26;
    Move(IDC_BTN_BHSTOP, m + 4, ly, 180, 28); Move(IDC_BTN_BHRESET, m + 190, ly, 120, 28);

    int tx = m + leftW + 10, tw = W - tx - m, th = H - top - m;
    MoveWindow(g_tabMain, tx, top, tw, th, TRUE);
    RECT tr = { tx, top, tx + tw, top + th };
    TabCtrl_AdjustRect(g_tabMain, FALSE, &tr);
    MoveWindow(g_log, tr.left, tr.top, tr.right - tr.left, tr.bottom - tr.top, TRUE);
    MoveWindow(g_tabTrain, tr.left, tr.top, tr.right - tr.left, tr.bottom - tr.top, TRUE);
    MoveWindow(g_tabUtil, tr.left, tr.top, tr.right - tr.left, tr.bottom - tr.top, TRUE);
    RECT sr = tr;
    TabCtrl_AdjustRect(g_tabUtil, FALSE, &sr);
    int x0 = sr.left + 4, y0 = sr.top + 4, cw = sr.right - sr.left - 8, ch = sr.bottom - sr.top - 8;
    int row = 30;

    // TRAIN > TOA DO
    MoveWindow(g_pts, x0, y0, cw, ch - 72, TRUE);
    ListView_SetColumnWidth(g_pts, 1, (std::max)(120, cw - 40 - 70 - 6));
    int by = y0 + ch - 64;
    Move(IDC_BTN_ADDPT, x0, by, cw, 28);
    Move(IDC_BTN_DELPT, x0, by + 34, 70, 26);
    Move(IDC_LBL_R, x0 + 90, by + 38, 60, 20);
    Move(IDC_EDIT_R, x0 + 150, by + 35, 40, 22);
    Move(IDC_BTN_SETR, x0 + 196, by + 34, 110, 26);

    // TRAIN > CAI DAT KHAC
    int r = y0 + 4;
    Move(IDC_LBL_TYPES, x0, r + 3, 90, 20);
    Move(IDC_CHK_T1, x0 + 95, r, 100, 24); Move(IDC_CHK_T2, x0 + 200, r, 90, 24); Move(IDC_CHK_T3, x0 + 295, r, 70, 24);
    r += row;
    Move(IDC_LBL_TRAINSET, x0, r + 3, 150, 20); Move(IDC_CMB_TRAINSET, x0 + 155, r, 170, 200);
    r += row + 6;
    Move(IDC_CHK_REST, x0, r, 120, 24); Move(IDC_EDIT_RESTN, x0 + 122, r + 1, 40, 22);
    Move(IDC_LBL_REST2, x0 + 168, r + 4, 130, 20); Move(IDC_EDIT_RESTMIN, x0 + 300, r + 1, 40, 22); Move(IDC_LBL_REST3, x0 + 345, r + 4, 40, 20);
    r += row + 10;
    Move(IDC_LBL_NOTE, x0, r, cw, 64);
    Move(IDC_BTN_APPLYALL, x0, y0 + ch - 28, 200, 26);

    // TRAIN > CHUYEN BAI
    r = y0 + 4;
    Move(IDC_CHK_SWTIME, x0, r, 150, 24); Move(IDC_EDIT_SWMIN, x0 + 152, r + 1, 44, 22); Move(IDC_LBL_SWMIN, x0 + 202, r + 4, 50, 20);
    r += row;
    Move(IDC_CHK_SWDEATH, x0, r, 250, 24);
    r += row + 6;
    Move(IDC_LBL_SWNOTE, x0, r, cw, 60);

    // KHUNG CAI DAT PHO BAN: phu len vung tab ben phai
    {
        g_pbRect = { tx, top, tx + tw, top + th };
        Move(IDC_PBP_TITLE, tx + 40, top + 6, tw - 80, 26);
        Move(IDC_PBP_CLOSE, tx + tw - 36, top + 6, 28, 26);
        int px = tx + 16, pw = tw - 32, E = 46, rh = 30;
        for (int i = 0; i < NPB; ++i) {
            int y2 = top + 44;
            Move(IDC_LBL_PBDONE0 + i, px, y2, pw, 20); y2 += 26;
            Move(IDC_LBL_PBRUNS0 + i, px, y2 + 3, 230, 20); Move(IDC_EDIT_PBRUNS0 + i, px + 235, y2, E, 22); y2 += rh;
            Move(IDC_LBL_PBREV0 + i, px, y2 + 3, 230, 20); Move(IDC_EDIT_PBREV0 + i, px + 235, y2, E, 22); y2 += rh;
            Move(PBX(i, PX_LSET), px, y2 + 3, 230, 20); Move(PBX(i, PX_SET), px + 235, y2, 170, 300); y2 += rh;
            if (i == 0) {
                Move(PBX(0, PX_LTSTOP), px, y2, 290, 24); Move(PBX(0, PX_LTY), px + 295, y2 + 1, E + 10, 22); y2 += rh;
                Move(PBX(0, PX_LTCONT), px + 22, y2, 300, 24); y2 += rh;
                Move(PBX(0, PX_LTLURE), px, y2, 290, 24); Move(PBX(0, PX_LTX), px + 295, y2 + 1, E + 10, 22); y2 += rh;
                Move(PBX(0, PX_LKPP), px + 22, y2 + 3, 170, 20); Move(PBX(0, PX_LTKPP), px + 195, y2, 36, 22); Move(PBX(0, PX_LKPP2), px + 236, y2 + 3, 140, 20); y2 += rh;
                Move(PBX(0, PX_LPICK), px, y2 + 3, 60, 20); Move(PBX(0, PX_LTPICK), px + 62, y2, (std::min)(330, pw - 62), 200); y2 += rh;
                Move(PBX(0, PX_LTSKIP), px, y2, pw, 24); y2 += rh;
            }
            if (i == 1) { Move(IDC_LBL_TQMINR, px, y2 + 3, 230, 20); Move(IDC_EDIT_TQMINR, px + 235, y2, E, 22); y2 += rh;
                          Move(PBX(1, PX_TQAFK), px, y2, pw, 24); y2 += rh;
                          Move(PBX(1, PX_TQBC), px, y2, pw, 24); y2 += rh; }
            if (i == 2) { Move(IDC_CHK_DTJUMP, px, y2, pw, 24); y2 += rh;
                          Move(IDC_LBL_DTJMAX, px + 22, y2 + 3, 70, 20); Move(IDC_CMB_DTJMAX, px + 95, y2, (std::min)(360, pw - 95), 200); y2 += rh; }
            if (i == 3) { Move(IDC_CHK_PTJUMP, px, y2, pw, 24); y2 += rh; Move(IDC_CHK_PTBOW, px, y2, pw, 24); y2 += rh; }
            if (i == 4) { Move(IDC_LBL_MCFARM, px, y2 + 3, 230, 20); Move(IDC_CMB_MCFARM, px + 235, y2, 110, 300); y2 += rh;
                          Move(IDC_LBL_MCBY, px + 22, y2 + 3, 110, 20); Move(IDC_CMB_MCBY, px + 135, y2, 170, 200); y2 += rh;
                          Move(IDC_LBL_MCMIN, px + 22, y2 + 3, 60, 20); Move(IDC_EDIT_MCMIN, px + 85, y2, 40, 22);
                          Move(IDC_LBL_MCLZ, px + 145, y2 + 3, 105, 20); Move(IDC_EDIT_MCLZ, px + 252, y2, 50, 22); y2 += rh;
                          Move(IDC_CHK_MCSKIP, px, y2, pw, 24); y2 += rh; }
            if (i == 1 || i == 2) { Move(PBX(i, PX_SFON), px, y2, 250, 24); Move(PBX(i, PX_SF), px + 255, y2 + 1, E, 22); y2 += rh; }
            Move(PBX(i, PX_LZON), px, y2, 180, 24); Move(PBX(i, PX_LZVAL), px + 183, y2 + 1, E, 22);
            Move(PBX(i, PX_LLZ), px + 236, y2 + 4, 75, 20); Move(PBX(i, PX_LZMIN), px + 312, y2 + 1, 40, 22); Move(PBX(i, PX_LLZM), px + 357, y2 + 4, 40, 20); y2 += rh;
            Move(PBX(i, PX_TIMEON), px, y2, 70, 24);
            Move(PBX(i, PX_H1), px + 72, y2 + 1, 30, 22); Move(PBX(i, PX_LH1), px + 106, y2 + 4, 26, 20);
            Move(PBX(i, PX_M1), px + 134, y2 + 1, 30, 22); Move(PBX(i, PX_LM1), px + 168, y2 + 4, 58, 20);
            Move(PBX(i, PX_H2), px + 228, y2 + 1, 30, 22); Move(PBX(i, PX_LH2), px + 262, y2 + 4, 26, 20);
            Move(PBX(i, PX_M2), px + 290, y2 + 1, 30, 22); Move(PBX(i, PX_LM2), px + 324, y2 + 4, 40, 20); y2 += rh;
            if (i == 1 || i == 2 || i == 3) { Move(PBX(i, PX_NMON), px, y2, 255, 24); Move(PBX(i, PX_NM), px + 258, y2 + 1, 40, 22); Move(PBX(i, PX_LNM), px + 303, y2 + 4, 40, 20); y2 += rh; }
            if (i == 0) { Move(PBX(0, PX_INFO), px, y2 + 4, pw, 36); y2 += rh + 16; }
            y2 += 6;
            Move(PBX(i, PX_NOTE), px, y2, pw, (std::max)(60, top + th - 8 - y2));
        }
    }

    // TIEN ICH > KY NANG
    Move(IDC_LBL_SKMODE, x0, y0 + 7, 90, 20); Move(IDC_CMB_SKMODE, x0 + 95, y0 + 4, 330, 200);
    Move(IDC_LBL_KEYS, x0 + 435, y0 + 7, 40, 20); Move(IDC_EDIT_KEYS, x0 + 475, y0 + 4, 90, 22);
    Move(IDC_LBL_SKSET, x0, y0 + 39, 120, 20); Move(IDC_CMB_SKSET, x0 + 125, y0 + 36, 170, 200);
    Move(IDC_BTN_GETSKILLS, x0 + 305, y0 + 35, 160, 26);
    {
        int ty = y0 + 68, th2 = ch - 68 - 36;
        MoveWindow(g_tabSk, x0, ty, cw, th2, TRUE);
        RECT kr = { x0, ty, x0 + cw, ty + th2 }; TabCtrl_AdjustRect(g_tabSk, FALSE, &kr);
        int kx = kr.left + 6, ky = kr.top + 6, kw = kr.right - kr.left - 12, kh = kr.bottom - kr.top - 12;
        Move(IDC_LBL_SKADD, kx, ky + 3, 70, 20); Move(IDC_CMB_SKADD, kx + 72, ky, (std::min)(320, kw - 120), 300); Move(IDC_BTN_SKADD, kx + 72 + (std::min)(320, kw - 120) + 6, ky - 1, 34, 24);
        MoveWindow(g_skOrder, kx, ky + 32, kw, kh - 32 - 34, TRUE);
        ListView_SetColumnWidth(g_skOrder, 1, (std::max)(150, kw - 55));
        Move(IDC_BTN_SKUP, kx, ky + kh - 28, 80, 26); Move(IDC_BTN_SKDOWN, kx + 86, ky + kh - 28, 80, 26); Move(IDC_BTN_SKDEL, kx + 172, ky + kh - 28, 80, 26);
        Move(IDC_CMB_SUPTYPE, kx, ky, 170, 200); Move(IDC_CMB_SUPSKILL, kx + 176, ky, (std::min)(260, kw - 290), 300);
        int px = kx + 176 + (std::min)(260, kw - 290) + 6;
        Move(IDC_EDIT_SUPPCT, px, ky, 36, 22); Move(IDC_LBL_SUPPCT, px + 40, ky + 3, 16, 20); Move(IDC_BTN_SUPADD, px + 58, ky - 1, 34, 24);
        MoveWindow(g_supList, kx, ky + 32, kw, kh - 32 - 100, TRUE);
        Move(IDC_BTN_SUPDEL, kx, ky + kh - 62, 80, 26);
        Move(IDC_LBL_SUPMP, kx + 100, ky + kh - 58, 250, 20); Move(IDC_EDIT_SUPMP, kx + 352, ky + kh - 61, 40, 22);
        Move(IDC_LBL_SUPNOTE, kx, ky + kh - 30, kw, 32);
    }
    Move(IDC_LBL_SKNOTE, x0, y0 + ch - 32, cw, 32);

    // TIEN ICH > THUOC
    r = y0 + 4;
    Move(IDC_LBL_POTMODE, x0, r + 3, 90, 20); Move(IDC_CMB_POTMODE, x0 + 95, r, 280, 200);
    r += row + 2;
    Move(IDC_LBL_HP, x0, r + 3, 170, 20); Move(IDC_EDIT_HP, x0 + 175, r, 40, 22);
    Move(IDC_LBL_HPKEY, x0 + 240, r + 3, 120, 20); Move(IDC_EDIT_HPKEY, x0 + 365, r, 40, 22);
    r += row;
    Move(IDC_LBL_MP, x0, r + 3, 170, 20); Move(IDC_EDIT_MP, x0 + 175, r, 40, 22);
    Move(IDC_LBL_MPKEY, x0 + 240, r + 3, 120, 20); Move(IDC_EDIT_MPKEY, x0 + 365, r, 40, 22);
    r += row + 4;
    MoveWindow(g_drugs, x0, r, (std::min)(cw, 360), 200, TRUE);
    Move(IDC_LBL_POTNOTE, x0, r + 206, cw, ch - (r + 206 - y0));

    // TIEN ICH > NHAT DO
    Move(IDC_LBL_PKMODE, x0, y0 + 7, 70, 20); Move(IDC_CMB_PKMODE, x0 + 75, y0 + 4, 200, 200);
    Move(IDC_BTN_PKDEFAULT, x0 + cw - 150, y0 + 3, 150, 26);
    int listH = (ch - 36) / 2 - 20;
    Move(IDC_EDIT_PKLIST, x0, y0 + 36, cw, listH);
    int by2 = y0 + 36 + listH + 6;
    Move(IDC_BTN_GETBAG, x0, by2, 170, 26); Move(IDC_BTN_ADDBAG, x0 + 176, by2, 150, 26);
    Move(IDC_BAGLIST, x0, by2 + 32, cw / 2, ch - (by2 + 32 - y0));
    Move(IDC_LBL_PKNOTE, x0 + cw / 2 + 8, by2 + 32, cw - cw / 2 - 8, ch - (by2 + 32 - y0));

    // TIEN ICH > SUA DO
    r = y0 + 4;
    Move(IDC_CHK_REPAIR, x0, r, cw, 24); r += row;
    Move(IDC_LBL_RTYPE, x0, r + 3, 90, 20); Move(IDC_CMB_RTYPE, x0 + 95, r, 380, 200); r += row + 10;
    Move(IDC_LBL_REPNOTE, x0, r, cw, 100);

    // TIEN ICH > BAN / HUY (khung soan dung chung)
    {
        r = y0 + 2;
        Move(IDC_CHK_BH, x0, r, cw, 24); r += 28;
        int lh = (std::max)(80, ch - 28 - 330);
        MoveWindow(g_rules, x0, r, cw, lh, TRUE); MoveWindow(g_rules2, x0, r, cw, lh, TRUE);
        ListView_SetColumnWidth(g_rules, 2, (std::max)(150, cw - 40 - 80 - 6)); ListView_SetColumnWidth(g_rules2, 2, (std::max)(150, cw - 40 - 80 - 6));
        r += lh + 6;
        Move(IDC_LBL_R_TITLE, x0, r + 3, 80, 20); Move(IDC_CMB_R_KIND, x0 + 82, r, 250, 200);
        Move(IDC_LBL_R_Q, x0 + 342, r + 3, 40, 20); Move(IDC_CMB_R_Q, x0 + 384, r, 160, 200); r += 28;
        Move(IDC_LBL_R_S, x0, r + 3, 60, 20); Move(IDC_EDIT_R_S, x0 + 62, r, 36, 22);
        Move(IDC_LBL_R_G, x0 + 110, r + 3, 70, 20); Move(IDC_CMB_R_LV, x0 + 182, r, 100, 300);
        Move(IDC_LBL_R_LOCK, x0 + 294, r + 3, 40, 20); Move(IDC_CMB_R_LOCK, x0 + 336, r, 150, 200); r += 28;
        Move(IDC_LBL_R_P, x0, r + 3, 80, 20);
        for (int i = 0; i < NSECTS; ++i) Move(IDC_CHK_SECT0 + i, x0 + 82 + i * 110, r, 105, 22);
        r += 26;
        Move(IDC_LBL_R_POS, x0, r + 3, 80, 20);
        int sw = (std::max)(110, (cw - 82) / 4);
        for (int i = 0; i < NSLOTS; ++i) Move(IDC_CHK_SLOT0 + i, x0 + 82 + (i % 4) * sw, r + (i / 4) * 24, sw - 4, 22);
        r += 3 * 24 + 4;
        Move(IDC_LBL_R_NAMES, x0, r + 3, 80, 20); Move(IDC_EDIT_R_NAMES, x0 + 82, r, cw - 82, 22); r += 28;
        Move(IDC_BTN_R_ADD, x0, r, 170, 26); Move(IDC_BTN_R_DEL, x0 + 176, r, 170, 26); r += 32;
        Move(IDC_LBL_FREEE, x0, r + 3, 200, 20); Move(IDC_EDIT_FREEE, x0 + 202, r, 36, 22);
        Move(IDC_LBL_FREEI, x0 + 260, r + 3, 200, 20); Move(IDC_EDIT_FREEI, x0 + 462, r, 36, 22);
        Move(IDC_LBL_BHNOTE2, x0, r, cw, 40);
        r += 28;
        Move(IDC_LBL_BHNOTE, x0, r, cw, (std::max)(20, ch - (r - y0)));
    }

    // TIEN ICH > AN TOAN
    r = y0 + 4;
    Move(IDC_CHK_FLOORQ, x0, r, cw, 24); r += row;
    Move(IDC_LBL_FLOORS, x0, r + 3, 290, 20); Move(IDC_EDIT_FLOORS, x0 + 292, r, 40, 22); r += row;
    Move(IDC_LBL_PROTECT, x0, r, cw, 20); r += 22;
    Move(IDC_EDIT_PROTECT, x0, r, cw, 60); r += 66;
    Move(IDC_BTN_DUMP, x0, r, 220, 26); r += 34;
    Move(IDC_LBL_SAFENOTE, x0, r, cw, ch - (r - y0));

    // TIEN ICH > CAI DAT GAME
    {
        int gy = y0 + 4, half = cw / 2;
        for (int i = 0; i < 14; ++i) {
            int id = i < 13 ? IDC_CHK_GS0 + i : IDC_CHK_HPFX;
            Move(id, x0 + (i % 2) * half, gy + (i / 2) * 26, half - 6, 24);
        }
        gy += 7 * 26 + 10;
        Move(IDC_CHK_CLOSEUI, x0, gy, cw, 24); gy += 32;
        Move(IDC_LBL_HIDELV, x0, gy + 3, 140, 20); Move(IDC_CMB_HIDELV, x0 + 142, gy, 330, 200); gy += 34;
        Move(IDC_LBL_GSNOTE, x0, gy, cw, 60);
    }
    // HIEU NANG
    MoveWindow(g_perf, tr.left + 6, tr.top + 6, tr.right - tr.left - 12, 8 * 22 + 30, TRUE);
    Move(IDC_BTN_PERFRESET, tr.left + 6, tr.top + 6 + 8 * 22 + 38, 150, 26);
    Move(IDC_LBL_PERFNOTE, tr.left + 6, tr.top + 6 + 8 * 22 + 72, tr.right - tr.left - 12, 60);

    // TAI KHOAN (tab chinh thu 4)
    {
        int ax = tr.left + 20, ay = tr.top + 14, lw = 90, ew = 260;
        Move(IDC_LBL_A_TITLE, ax, ay, 420, 22); ay += 36;
        Move(IDC_LBL_A_USER, ax, ay + 3, lw, 20); Move(IDC_EDIT_A_USER, ax + lw, ay, ew, 24); ay += 34;
        Move(IDC_LBL_A_PASS, ax, ay + 3, lw, 20); Move(IDC_EDIT_A_PASS, ax + lw, ay, ew, 24); Move(IDC_BTN_A_EYE, ax + lw + ew + 6, ay, 50, 24); ay += 34;
        Move(IDC_LBL_A_SV, ax, ay + 3, lw, 20); Move(IDC_EDIT_A_SV, ax + lw, ay, 80, 24); ay += 34;
        Move(IDC_LBL_A_KENH, ax, ay + 3, lw, 20); Move(IDC_CMB_A_KENH, ax + lw, ay, 140, 300); ay += 34;
        Move(IDC_LBL_A_NV, ax, ay + 3, lw, 20); Move(IDC_EDIT_A_NV, ax + lw, ay, ew, 24); ay += 44;
        Move(IDC_BTN_A_SAVE, ax + lw, ay, 120, 30); Move(IDC_BTN_A_LOGIN, ax + lw + 130, ay, 130, 30); Move(IDC_BTN_A_CANCEL, ax + lw + 270, ay, 80, 30); ay += 44;
        Move(IDC_LBL_A_NOTE, ax, ay, (std::max)(300, (int)(tr.right - tr.left) - 40), 110);
    }

    // TIEN ICH > DI CHUYEN
    r = y0 + 4;
    Move(IDC_CHK_TELE, x0, r, cw, 24); r += row;
    Move(IDC_CHK_VLL, x0, r, cw, 24); r += row + 10;
    Move(IDC_LBL_MOVENOTE, x0, r, cw, 120);
    UpdatePages();
}

// ------------------------------------------------------------ tao giao dien

static void AddColumn(HWND lv, int i, const wchar_t* text, int w) {
    LVCOLUMNW c = {}; c.mask = LVCF_TEXT | LVCF_WIDTH; c.pszText = (LPWSTR)text; c.cx = w;
    SendMessageW(lv, LVM_INSERTCOLUMNW, i, (LPARAM)&c);
}
static void AddTab(HWND tab, int i, const wchar_t* text) {
    TCITEMW t = {}; t.mask = TCIF_TEXT; t.pszText = (LPWSTR)text;
    SendMessageW(tab, TCM_INSERTITEMW, i, (LPARAM)&t);
}
static HWND Combo(int id, std::vector<HWND>* page, std::initializer_list<const wchar_t*> items) {
    HWND h = Ctl(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, id, page);
    AddItems(h, items);
    return h;
}
static HWND Label(int id, const wchar_t* text, std::vector<HWND>* page) { return Ctl(L"STATIC", text, SS_LEFT, id, page); }
static HWND Edit(int id, std::vector<HWND>* page, DWORD extra = 0) { return Ctl(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | extra, id, page); }
static HWND Box(int id, const wchar_t* text, std::vector<HWND>* page) { return Ctl(L"BUTTON", text, BS_AUTOCHECKBOX, id, page); }

static void CreateUI() {
    NONCLIENTMETRICSW ncm = { sizeof(ncm) };
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    g_font = CreateFontIndirectW(&ncm.lfMessageFont);
    LOGFONTW lb = ncm.lfMessageFont; lb.lfWeight = FW_BOLD; lb.lfHeight = lb.lfHeight * 5 / 4;
    g_fontBold = CreateFontIndirectW(&lb);
    g_brLog = CreateSolidBrush(RGB(43, 43, 43));
    g_brBlue = CreateSolidBrush(RGB(51, 122, 183));
    g_brYellow = CreateSolidBrush(RGB(252, 229, 150));
    g_brGray = CreateSolidBrush(RGB(236, 236, 236));
    g_brGreen = CreateSolidBrush(RGB(92, 184, 92));

    // Tab tao truoc de nam duoi cac control khac
    g_tabMain = Ctl(WC_TABCONTROLW, L"", WS_CLIPSIBLINGS, IDC_TAB_MAIN);
    AddTab(g_tabMain, 0, L"HOẠT ĐỘNG"); AddTab(g_tabMain, 1, L"TRAIN"); AddTab(g_tabMain, 2, L"TIỆN ÍCH"); AddTab(g_tabMain, 3, L"TÀI KHOẢN");
    AddTab(g_tabMain, 4, L"HIỆU NĂNG");
    g_tabTrain = Ctl(WC_TABCONTROLW, L"", WS_CLIPSIBLINGS, IDC_TAB_TRAIN);
    AddTab(g_tabTrain, 0, L"TỌA ĐỘ"); AddTab(g_tabTrain, 1, L"CÀI ĐẶT KHÁC"); AddTab(g_tabTrain, 2, L"CHUYỂN BÃI");
    g_tabUtil = Ctl(WC_TABCONTROLW, L"", WS_CLIPSIBLINGS | TCS_MULTILINE, IDC_TAB_UTIL);
    AddTab(g_tabUtil, 0, L"KỸ NĂNG"); AddTab(g_tabUtil, 1, L"THUỐC"); AddTab(g_tabUtil, 2, L"NHẶT ĐỒ"); AddTab(g_tabUtil, 3, L"SỬA ĐỒ");
    AddTab(g_tabUtil, 4, L"BÁN"); AddTab(g_tabUtil, 5, L"HỦY"); AddTab(g_tabUtil, 6, L"AN TOÀN"); AddTab(g_tabUtil, 7, L"DI CHUYỂN");
    AddTab(g_tabUtil, 8, L"CÀI ĐẶT GAME");

    g_acc = Ctl(WC_LISTVIEWW, L"", LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_BORDER, IDC_ACCLIST);
    ListView_SetExtendedListViewStyle(g_acc, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    AddColumn(g_acc, 0, L"Tài khoản", 110); AddColumn(g_acc, 1, L"Nhân vật", 110); AddColumn(g_acc, 2, L"Trạng thái", 95);
    AddColumn(g_acc, 3, L"Cấp", 40); AddColumn(g_acc, 4, L"Máu", 85); AddColumn(g_acc, 5, L"Vị trí", 160);
    AddColumn(g_acc, 6, L"Giết / Chết / Nhặt", 105); AddColumn(g_acc, 7, L"VIP", 45); AddColumn(g_acc, 8, L"Đồng nhặt", 80);
    AddColumn(g_acc, 9, L"CPU", 55);

    Ctl(L"STATIC", L"", SS_LEFT, IDC_CONNINFO);

    HWND lbl = Ctl(L"STATIC", L"ĐIỀU KHIỂN", SS_LEFT, IDC_LBL_CTRL);
    SendMessageW(lbl, WM_SETFONT, (WPARAM)g_fontBold, TRUE);
    for (int i = 0; i < NPB; ++i) {
        Ctl(L"BUTTON", (std::wstring(L"[PB] ") + PB_NAME[i]).c_str(), BS_AUTOCHECKBOX, IDC_CHK_PB0 + i);
        g_pbState[i] = Ctl(L"STATIC", L"Chưa làm", SS_CENTER | SS_CENTERIMAGE, IDC_PB_ST0 + i);
        Ctl(L"BUTTON", L"...", BS_PUSHBUTTON, IDC_BTN_PBG0 + i);
    }
    Ctl(L"BUTTON", L"Đánh quái (Train)", BS_AUTOCHECKBOX, IDC_CHK_TRAIN);
    g_trainState = Ctl(L"STATIC", L"Chưa làm", SS_CENTER | SS_CENTERIMAGE, IDC_TRAIN_STATE);
    Ctl(L"BUTTON", L"...", BS_PUSHBUTTON, IDC_BTN_GEAR);
    HWND vip = Ctl(L"STATIC", L"VIP: -", SS_LEFT | SS_NOTIFY, IDC_LBL_VIP);
    Ctl(L"STATIC", L"", SS_LEFT, IDC_LBL_STAT1);
    Ctl(L"STATIC", L"", SS_LEFT, IDC_LBL_STAT2);
    Ctl(L"STATIC", L"", SS_LEFT, IDC_LBL_STAT3);
    Ctl(L"BUTTON", L"Đặt lại thống kê", BS_PUSHBUTTON, IDC_BTN_STATRESET);
    Ctl(L"STATIC", L"", SS_LEFT, IDC_LBL_BHSTATE);
    Ctl(L"BUTTON", L"DỪNG BÁN/HỦY (khẩn cấp)", BS_PUSHBUTTON, IDC_BTN_BHSTOP);
    Ctl(L"BUTTON", L"Bật lại bán/hủy", BS_PUSHBUTTON, IDC_BTN_BHRESET);

    // tooltip cho o VIP
    g_tip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, g_hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(g_tip, TTM_SETMAXTIPWIDTH, 0, 400);
    SendMessageW(g_tip, TTM_SETDELAYTIME, TTDT_AUTOPOP, 20000);
    TOOLINFOW ti = { sizeof(ti) }; ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS; ti.hwnd = g_hwnd; ti.uId = (UINT_PTR)vip;
    ti.lpszText = (LPWSTR)L"Chưa có dữ liệu";
    SendMessageW(g_tip, TTM_ADDTOOLW, 0, (LPARAM)&ti);

    g_log = Ctl(L"EDIT", L"", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL, IDC_LOG, &g_pageLog);

    // ---- TRAIN > TOA DO
    auto* P = &g_pgTrain[0];
    g_pts = Ctl(WC_LISTVIEWW, L"", LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_BORDER, IDC_PTLIST, P);
    ListView_SetExtendedListViewStyle(g_pts, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    AddColumn(g_pts, 0, L"#", 40); AddColumn(g_pts, 1, L"Vị trí đánh quái", 220); AddColumn(g_pts, 2, L"Phạm vi", 70);
    Ctl(L"BUTTON", L"Thêm tọa độ hiện tại", BS_PUSHBUTTON, IDC_BTN_ADDPT, P);
    Ctl(L"BUTTON", L"Xóa", BS_PUSHBUTTON, IDC_BTN_DELPT, P);
    Label(IDC_LBL_R, L"Phạm vi:", P);
    Ctl(L"EDIT", L"5", ES_NUMBER | WS_BORDER, IDC_EDIT_R, P);
    Ctl(L"BUTTON", L"Đặt cho dòng chọn", BS_PUSHBUTTON, IDC_BTN_SETR, P);

    // ---- TRAIN > CAI DAT KHAC
    P = &g_pgTrain[1];
    Label(IDC_LBL_TYPES, L"Loại quái:", P);
    Box(IDC_CHK_T1, L"Quái thường", P); Box(IDC_CHK_T2, L"Tinh anh", P); Box(IDC_CHK_T3, L"Boss", P);
    Label(IDC_LBL_TRAINSET, L"Nhóm kỹ năng khi train:", P);
    Combo(IDC_CMB_TRAINSET, P, { L"Nhóm kỹ năng 1", L"Nhóm kỹ năng 2", L"Nhóm kỹ năng 3", L"Nhóm kỹ năng 4", L"Nhóm kỹ năng 5" });
    Box(IDC_CHK_REST, L"Khi bị giết >=", P);
    Edit(IDC_EDIT_RESTN, P, ES_NUMBER);
    Label(IDC_LBL_REST2, L"lần: về thành nghỉ", P);
    Edit(IDC_EDIT_RESTMIN, P, ES_NUMBER);
    Label(IDC_LBL_REST3, L"phút", P);
    Label(IDC_LBL_NOTE, L"Kỹ năng, thuốc, nhặt đồ, sửa đồ, bán/hủy và di chuyển nằm ở tab TIỆN ÍCH (dùng chung cho mọi hoạt động). "
                        L"Sửa khi đang train thì tự áp dụng lại sau 1-2 giây.", P);
    Ctl(L"BUTTON", L"Áp dụng cài đặt cho tất cả acc", BS_PUSHBUTTON, IDC_BTN_APPLYALL, P);

    // ---- TRAIN > CHUYEN BAI
    P = &g_pgTrain[2];
    Box(IDC_CHK_SWTIME, L"Chuyển bãi sau mỗi", P);
    Edit(IDC_EDIT_SWMIN, P, ES_NUMBER);
    Label(IDC_LBL_SWMIN, L"phút", P);
    Box(IDC_CHK_SWDEATH, L"Chuyển bãi mỗi khi bị giết", P);
    Label(IDC_LBL_SWNOTE, L"Xoay vòng theo thứ tự các tọa độ đang tick ở tab TỌA ĐỘ. Cần ít nhất 2 tọa độ.", P);

    // ---- KHUNG CAI DAT PHO BAN (moi pho ban mot khung)
    P = &g_pgPbCommon;
    { HWND t = Ctl(L"STATIC", L"", SS_CENTER | SS_CENTERIMAGE, IDC_PBP_TITLE, P); SendMessageW(t, WM_SETFONT, (WPARAM)g_fontBold, TRUE); }
    Ctl(L"BUTTON", L"X", BS_PUSHBUTTON, IDC_PBP_CLOSE, P);
    for (int i = 0; i < NPB; ++i) {
        P = &g_pgPb[i];
        Label(IDC_LBL_PBDONE0 + i, L"", P);
        Label(IDC_LBL_PBRUNS0 + i, L"Phạm vi tìm quái (ô, 99 = cả map):", P);
        Edit(IDC_EDIT_PBRUNS0 + i, P, ES_NUMBER);
        Label(IDC_LBL_PBREV0 + i, L"Số hoa hồi sinh trong phó bản:", P);
        Edit(IDC_EDIT_PBREV0 + i, P, ES_NUMBER);
        Label(PBX(i, PX_LSET), L"Bộ kỹ năng (tab KỸ NĂNG):", P);
        Combo(PBX(i, PX_SET), P, { L"Giống Đánh quái", L"Nhóm kỹ năng 1", L"Nhóm kỹ năng 2", L"Nhóm kỹ năng 3", L"Nhóm kỹ năng 4", L"Nhóm kỹ năng 5" });
        if (i == 0) {
            Box(PBX(0, PX_LTSTOP), L"Dừng lấy liên trảm khi liên trảm lớn hơn", P); Edit(PBX(0, PX_LTY), P, ES_NUMBER);
            Box(PBX(0, PX_LTCONT), L"Vẫn tiếp tục cho đến khi đứt chuỗi", P);
            Box(PBX(0, PX_LTLURE), L"Dụ quái đi theo khi số liên trảm lớn hơn", P); Edit(PBX(0, PX_LTX), P, ES_NUMBER);
            Label(PBX(0, PX_LKPP), L"Khi dụ quái: mỗi điểm giết", P); Edit(PBX(0, PX_LTKPP), P, ES_NUMBER); Label(PBX(0, PX_LKPP2), L"con rồi đi tiếp", P);
            Label(PBX(0, PX_LPICK), L"Nhặt đồ:", P);
            Combo(PBX(0, PX_LTPICK), P, { L"Tích liên trảm — không nhặt đồ", L"Vừa tích liên trảm vừa nhặt đồ (ưu tiên nhặt)" });
            Box(PBX(0, PX_LTSKIP), L"Bỏ qua đánh BOSS (đạt mốc thì ra khỏi phó bản)", P);
        }
        if (i == 1) { Label(IDC_LBL_TQMINR, L"Làm khi đủ (hoa hồng):", P); Edit(IDC_EDIT_TQMINR, P, ES_NUMBER);
                      Box(PBX(1, PX_TQAFK), L"Đi hết vòng tuần không thấy quái (cổng chưa mở): bật treo máy của game, phạm vi 99, tối đa 3 phút", P);
                      Box(PBX(1, PX_TQBC), L"Bỏ qua đánh Boss ở trạng thái đếm số (boss mang buff bất tử / bảo hộ)", P); }
        if (i == 2) { Box(IDC_CHK_DTJUMP, L"Nhảy quanh quái ở mọi ải (ải game cho nhảy, cần thể lực >= 20)", P);
                      Label(IDC_LBL_DTJMAX, L"Tầm nhảy:", P);
                      Combo(IDC_CMB_DTJMAX, P, { L"Theo game (JUMP_MAX_DIS, mặc định 8 ô) — như bot gốc", L"500 (thử nghiệm, như bản cũ)" }); }
        if (i == 3) { Box(IDC_CHK_PTJUMP, L"Nhảy khi đánh Khôi Khôi (lúc boss vào trạng thái đặc biệt)", P); Box(IDC_CHK_PTBOW, L"Xong thì bật lại cung", P); }
        if (i == 4) {
            Label(IDC_LBL_MCFARM, L"Dừng lại treo quái (tại 76,51) ở:", P);
            HWND cb = Combo(IDC_CMB_MCFARM, P, { L"Không dừng" });
            for (int f = 1; f <= 16; ++f) SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)(L"Tầng " + std::to_wstring(f)).c_str());
            Label(IDC_LBL_MCBY, L"Ngưng treo khi:", P);
            Combo(IDC_CMB_MCBY, P, { L"Hết số phút", L"Đạt mốc liên trảm" });
            Label(IDC_LBL_MCMIN, L"Số phút:", P); Edit(IDC_EDIT_MCMIN, P, ES_NUMBER);
            Label(IDC_LBL_MCLZ, L"Mốc liên trảm:", P); Edit(IDC_EDIT_MCLZ, P, ES_NUMBER);
            Box(IDC_CHK_MCSKIP, L"Bỏ qua ải chuột (tầng 15) và phòng thần bí: không đánh, đi thẳng tới cửa / cổng", P);
        }
        if (i == 1 || i == 2) { Box(PBX(i, PX_SFON), L"Dừng phó bản sau khi vượt qua ải", P); Edit(PBX(i, PX_SF), P, ES_NUMBER); }
        Box(PBX(i, PX_LZON), PB_LZ_GE[i] ? L"Làm khi liên trảm từ" : L"Làm khi liên trảm dưới", P); Edit(PBX(i, PX_LZVAL), P, ES_NUMBER);
        Label(PBX(i, PX_LLZ), PB_LZ_GE[i] ? L"và còn từ" : L"hoặc ít hơn", P); Edit(PBX(i, PX_LZMIN), P, ES_NUMBER); Label(PBX(i, PX_LLZM), L"phút", P);
        Box(PBX(i, PX_TIMEON), L"Làm từ", P);
        Edit(PBX(i, PX_H1), P, ES_NUMBER); Label(PBX(i, PX_LH1), L"giờ", P); Edit(PBX(i, PX_M1), P, ES_NUMBER); Label(PBX(i, PX_LM1), L"phút đến", P);
        Edit(PBX(i, PX_H2), P, ES_NUMBER); Label(PBX(i, PX_LH2), L"giờ", P); Edit(PBX(i, PX_M2), P, ES_NUMBER); Label(PBX(i, PX_LM2), L"phút", P);
        if (i == 1 || i == 2 || i == 3) { Box(PBX(i, PX_NMON), L"Rời phó bản khi không có quái trong", P); Edit(PBX(i, PX_NM), P, ES_NUMBER); Label(PBX(i, PX_LNM), L"phút", P); }
        if (i == 0) Label(PBX(0, PX_INFO), L"", P);
        static const wchar_t* notes[NPB] = {
            L"Đi nhanh theo tuyến LienTram.xml, không đứng chờ. Dưới mốc dụ quái: dọn lính quanh mỗi điểm. Từ mốc dụ quái: trên đường không dừng đánh để lính bám theo, "
            L"tới điểm giết 1–2 con rồi đi tiếp. Thời gian giữ chuỗi còn dưới 40% thì dừng đánh con gần nhất, ra chiêu dồn. Boss chỉ đánh khi đạt mốc (hoặc hết vòng không thấy lính).",
            L"Nhặt đồ trước rồi mới đánh (luôn nhặt hoa hồng + đồng). Chỉ sang tầng khi cổng tầng kế đã mở và hết quái 1 giây. Mỗi lần qua ải tốn 1 hoa. Tuyến: data\\phoban\\ThienQuan.xml.",
            L"Ải chuột: đánh từng con, nhặt hết đồng mới đánh tiếp. Tuyến: data\\phoban\\DoanhTrai.xml.",
            L"Trước boss không dùng buff / hồi máu, tạm tắt ám khí + cung + cất thú chiến (xong trả lại).",
            L"Leo tầng: không đánh, không nhặt, đi thẳng tới cửa. Tầng treo: đứng ở (76,51), quái tới thì đánh, nhặt theo cài đặt nhặt của Đánh quái. "
            L"Phòng thần bí: MeCungThanBi.xml; ải chuột tầng 15: MeCung15.xml." };
        Label(PBX(i, PX_NOTE), (std::wstring(notes[i]) + L"\r\nĐiều kiện liên trảm đọc buff liên trảm của nhân vật (mức + thời gian còn). "
                                L"Chết quá số hoa hồi sinh thì về thành, lượt đó tính thất bại. Đi tới khi NPC báo hết lượt (số lượt đã đi / tối đa đọc từ bảng phó bản của NPC).").c_str(), P);
    }

    // ---- TIEN ICH > KY NANG
    P = &g_pgUtil[0];
    Label(IDC_LBL_SKMODE, L"Cách ra chiêu:", P);
    Combo(IDC_CMB_SKMODE, P, { L"Theo nhóm kỹ năng (nhóm trống thì dùng skill ở 5 ô đầu)", L"Bấm phím thông thường" });
    Label(IDC_LBL_KEYS, L"Phím:", P);
    Edit(IDC_EDIT_KEYS, P);
    Label(IDC_LBL_SKSET, L"Chọn nhóm kỹ năng:", P);
    Combo(IDC_CMB_SKSET, P, { L"Nhóm kỹ năng 1", L"Nhóm kỹ năng 2", L"Nhóm kỹ năng 3", L"Nhóm kỹ năng 4", L"Nhóm kỹ năng 5" });
    Ctl(L"BUTTON", L"Lấy danh sách skill", BS_PUSHBUTTON, IDC_BTN_GETSKILLS, P);
    g_tabSk = Ctl(WC_TABCONTROLW, L"", WS_CLIPSIBLINGS, IDC_TAB_SK, P);
    AddTab(g_tabSk, 0, L"MÔN PHÁI"); AddTab(g_tabSk, 1, L"GIANG HỒ"); AddTab(g_tabSk, 2, L"BANG"); AddTab(g_tabSk, 3, L"KHÁC"); AddTab(g_tabSk, 4, L"HỖ TRỢ");
    auto* G = &g_pageSkGroup;
    Label(IDC_LBL_SKADD, L"Kỹ năng:", G);
    Ctl(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, IDC_CMB_SKADD, G);
    Ctl(L"BUTTON", L"+", BS_PUSHBUTTON, IDC_BTN_SKADD, G);
    g_skOrder = Ctl(WC_LISTVIEWW, L"", LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_BORDER | LVS_NOSORTHEADER, IDC_SKLIST, G);
    ListView_SetExtendedListViewStyle(g_skOrder, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    AddColumn(g_skOrder, 0, L"STT", 45); AddColumn(g_skOrder, 1, L"Kéo thả để sắp xếp thứ tự dùng", 320);
    Ctl(L"BUTTON", L"▲ Lên", BS_PUSHBUTTON, IDC_BTN_SKUP, G);
    Ctl(L"BUTTON", L"▼ Xuống", BS_PUSHBUTTON, IDC_BTN_SKDOWN, G);
    Ctl(L"BUTTON", L"Xóa", BS_PUSHBUTTON, IDC_BTN_SKDEL, G);
    auto* S2 = &g_pageSkSup;
    Combo(IDC_CMB_SUPTYPE, S2, { L"Hồi máu khi máu dưới", L"Buff khi hết buff" });
    Ctl(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, IDC_CMB_SUPSKILL, S2);
    Edit(IDC_EDIT_SUPPCT, S2, ES_NUMBER);
    Label(IDC_LBL_SUPPCT, L"%", S2);
    Ctl(L"BUTTON", L"+", BS_PUSHBUTTON, IDC_BTN_SUPADD, S2);
    g_supList = Ctl(WC_LISTVIEWW, L"", LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_BORDER | LVS_NOSORTHEADER, IDC_SUPLIST, S2);
    ListView_SetExtendedListViewStyle(g_supList, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    AddColumn(g_supList, 0, L"Loại", 80); AddColumn(g_supList, 1, L"Kỹ năng", 220); AddColumn(g_supList, 2, L"Điều kiện", 120);
    Ctl(L"BUTTON", L"Xóa", BS_PUSHBUTTON, IDC_BTN_SUPDEL, S2);
    Label(IDC_LBL_SUPMP, L"Ngưng dùng hỗ trợ khi nội lực dưới (%):", S2);
    Edit(IDC_EDIT_SUPMP, S2, ES_NUMBER);
    Label(IDC_LBL_SUPNOTE, L"Hỗ trợ dùng lên bản thân. Hồi máu theo mốc máu riêng từng skill; buff chỉ dùng khi buff đã hết. "
                           L"Nội lực dưới mốc thì ngưng cả hai để khỏi cạn nội lực.", S2);
    Label(IDC_LBL_SKNOTE, L"Dùng skill đầu tiên sẵn sàng theo thứ tự. Mỗi tab có hồi chiêu chung riêng: tab này đang hồi thì dùng tab khác. "
                          L"Chọn nhóm cho từng hoạt động (Train: tab TRAIN > CÀI ĐẶT KHÁC).", P);

    // ---- TIEN ICH > THUOC
    P = &g_pgUtil[1];
    Label(IDC_LBL_POTMODE, L"Cách dùng:", P);
    Combo(IDC_CMB_POTMODE, P, { L"Bấm phím tắt (thuốc đặt trên ô phím)", L"Dùng thuốc trong túi theo loại tick bên dưới" });
    Label(IDC_LBL_HP, L"Bơm máu khi dưới (%)", P); Edit(IDC_EDIT_HP, P, ES_NUMBER);
    Label(IDC_LBL_HPKEY, L"Phím bình máu:", P); Edit(IDC_EDIT_HPKEY, P, ES_NUMBER);
    Label(IDC_LBL_MP, L"Bơm nội lực khi dưới (%)", P); Edit(IDC_EDIT_MP, P, ES_NUMBER);
    Label(IDC_LBL_MPKEY, L"Phím bình nội lực:", P); Edit(IDC_EDIT_MPKEY, P, ES_NUMBER);
    g_drugs = Ctl(WC_LISTVIEWW, L"", LVS_REPORT | LVS_SINGLESEL | WS_BORDER | LVS_NOSORTHEADER, IDC_DRUGLIST, P);
    ListView_SetExtendedListViewStyle(g_drugs, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    AddColumn(g_drugs, 0, L"Thuốc", 140); AddColumn(g_drugs, 1, L"Loại", 90); AddColumn(g_drugs, 2, L"ID", 70);
    for (int i = 0; i < NDRUGS; ++i) {
        int rr = LvInsert(g_drugs, i, DRUGS[i].name);
        LvText(g_drugs, rr, 1, DRUGS[i].hp ? L"Máu" : L"Nội lực");
        LvText(g_drugs, rr, 2, std::to_wstring(DRUGS[i].id));
    }
    Label(IDC_LBL_POTNOTE, L"Theo túi: ngoài phó bản dùng loại nhỏ trước (Quy Đơn → Thần Đơn, Ninh Hoàn → Bách Hoàn), trong phó bản dùng loại lớn trước. "
                           L"Thuốc đang tick được bảo vệ, không bao giờ bị bán/hủy (muốn hủy loại nào thì bỏ tick trước). "
                           L"Để trống ô phím nếu không dùng phím tắt.", P);

    // ---- TIEN ICH > NHAT DO
    P = &g_pgUtil[2];
    Label(IDC_LBL_PKMODE, L"Nhặt đồ:", P);
    Combo(IDC_CMB_PKMODE, P, { L"Không nhặt", L"Chỉ nhặt theo danh sách", L"Nhặt tất cả" });
    Ctl(L"EDIT", L"", WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL, IDC_EDIT_PKLIST, P);
    Ctl(L"BUTTON", L"Lấy tên đồ trong túi", BS_PUSHBUTTON, IDC_BTN_GETBAG, P);
    Ctl(L"LISTBOX", L"", WS_BORDER | WS_VSCROLL | LBS_NOTIFY | LBS_EXTENDEDSEL, IDC_BAGLIST, P);
    Ctl(L"BUTTON", L"Thêm vào danh sách", BS_PUSHBUTTON, IDC_BTN_ADDBAG, P);
    Ctl(L"BUTTON", L"Danh sách mặc định", BS_PUSHBUTTON, IDC_BTN_PKDEFAULT, P);
    Label(IDC_LBL_PKNOTE, L"Cách nhau dấu phẩy, đứng trước ưu tiên hơn; khớp khi tên chứa từ khóa. [Đồng] = tiền, "
                          L"[Mảnh] = mọi món bắt đầu bằng \"Mảnh\", [Hoa Hồng], [Mảnh Trận Pháp], re:<biểu thức> = regex. "
                          L"Chỉ nhặt trong bán kính train; nhặt xong mới đánh. Đồng chỉ được tính khi số đồng thật sự tăng "
                          L"(người khác nhặt mất thì ghi \"Mất\").", P);

    // ---- TIEN ICH > SUA DO
    P = &g_pgUtil[3];
    Box(IDC_CHK_REPAIR, L"Tự về thành sửa khi có trang bị hỏng (độ bền về 0)", P);
    Label(IDC_LBL_RTYPE, L"Kiểu sửa:", P);
    Combo(IDC_CMB_RTYPE, P, { L"Sửa thường", L"Sửa đặc biệt (tốn nhiều đồng hơn, không giảm độ bền tối đa)" });
    Label(IDC_LBL_REPNOTE, L"Sửa ở NPC Vũ Khí (Tương Dương). Đang trong phó bản thì chờ ra khỏi phó bản mới về. "
                           L"Có VIP (Võ Lâm Lệnh) thì truyền tống miễn phí về thành và quay lại bãi; không có VIP thì đi bộ. "
                           L"Chuyến về thành làm luôn việc bán đồ nếu tab BÁN / HỦY có quy tắc bán.", P);

    // ---- TIEN ICH > BAN  /  HUY: moi trang 1 danh sach; khung soan quy tac dung chung (thuoc ca 2 trang)
    {
        auto* PB = &g_pgUtil[4]; auto* PH = &g_pgUtil[5];
        auto both = [&](HWND h) { PB->push_back(h); PH->push_back(h); };
        Box(IDC_CHK_BH, L"Bật bán / hủy (tick cột # = LÀM THẬT; không tick = chạy thử, chỉ ghi log)", nullptr); both(Item(IDC_CHK_BH));
        g_rules = Ctl(WC_LISTVIEWW, L"", LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_BORDER, IDC_RULELIST, PB);
        g_rules2 = Ctl(WC_LISTVIEWW, L"", LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_BORDER, IDC_RULELIST2, PH);
        for (HWND lv : { g_rules, g_rules2 }) {
            ListView_SetExtendedListViewStyle(lv, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
            AddColumn(lv, 0, L"#", 40); AddColumn(lv, 1, L"Chế độ", 80); AddColumn(lv, 2, L"Quy tắc", 400);
        }
        both(Label(IDC_LBL_R_TITLE, L"Quy tắc mới:", nullptr));
        both(Ctl(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, IDC_CMB_R_KIND));
        AddItems(Item(IDC_CMB_R_KIND), { L"Trang bị theo thuộc tính", L"Vật phẩm theo tên (đúng nguyên tên)" });
        both(Label(IDC_LBL_R_Q, L"Màu:", nullptr));
        both(Ctl(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, IDC_CMB_R_Q));
        AddItems(Item(IDC_CMB_R_Q), { QNAMES[0], QNAMES[1], QNAMES[2], QNAMES[3], QNAMES[4] });
        both(Label(IDC_LBL_R_S, L"Số sao ≤", nullptr)); both(Edit(IDC_EDIT_R_S, nullptr, ES_NUMBER));
        both(Label(IDC_LBL_R_G, L"Cấp dùng ≤", nullptr));
        both(Ctl(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, IDC_CMB_R_LV));
        SendMessageW(Item(IDC_CMB_R_LV), CB_ADDSTRING, 0, (LPARAM)L"Không xét");
        for (int lv = 10; lv <= 150; lv += 10) { std::wstring t = L"≤ " + std::to_wstring(lv); SendMessageW(Item(IDC_CMB_R_LV), CB_ADDSTRING, 0, (LPARAM)t.c_str()); }
        both(Label(IDC_LBL_R_LOCK, L"Khóa:", nullptr));
        both(Ctl(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, IDC_CMB_R_LOCK));
        AddItems(Item(IDC_CMB_R_LOCK), { L"Bất kỳ", L"Chỉ đồ khóa", L"Chỉ đồ không khóa" });
        both(Label(IDC_LBL_R_P, L"Môn phái:", nullptr));
        for (int i = 0; i < NSECTS; ++i) both(Box(IDC_CHK_SECT0 + i, SECTS[i].name, nullptr));
        both(Label(IDC_LBL_R_POS, L"Loại trang bị:", nullptr));
        for (int i = 0; i < NSLOTS; ++i) both(Box(IDC_CHK_SLOT0 + i, SLOTS[i].name, nullptr));
        both(Label(IDC_LBL_R_NAMES, L"Tên:", nullptr)); both(Edit(IDC_EDIT_R_NAMES, nullptr));
        both(Ctl(L"BUTTON", L"Thêm quy tắc (chạy thử)", BS_PUSHBUTTON, IDC_BTN_R_ADD));
        both(Ctl(L"BUTTON", L"Xóa quy tắc đang chọn", BS_PUSHBUTTON, IDC_BTN_R_DEL));
        Label(IDC_LBL_FREEE, L"Về bán trang bị khi ô trống ≤", PB); Edit(IDC_EDIT_FREEE, PB, ES_NUMBER);
        Label(IDC_LBL_FREEI, L"Về bán vật phẩm khi ô trống ≤", PB); Edit(IDC_EDIT_FREEI, PB, ES_NUMBER);
        Label(IDC_LBL_BHNOTE, L"Bán ở NPC Vũ Khí (không nhận thì Tạp Hóa), đi khi túi còn ít ô trống. Không tick điều kiện nào = không xét điều kiện đó.", PB);
        Label(IDC_LBL_BHNOTE2, L"Hủy làm tại chỗ (kể cả trong phó bản). Cấp dùng = cấp nhân vật cần để mặc: chọn ≤ 40 thì đồ cấp 10–40 khớp, đồ cấp 50 trở lên được giữ.", PH);
    }

    // ---- TIEN ICH > AN TOAN
    P = &g_pgUtil[6];
    Box(IDC_CHK_FLOORQ, L"Bảo vệ đồ màu Tím trở lên (không bao giờ bán/hủy)", P);
    Label(IDC_LBL_FLOORS, L"Bảo vệ trang bị cường hóa từ + (0 = tắt):", P); Edit(IDC_EDIT_FLOORS, P, ES_NUMBER);
    Label(IDC_LBL_PROTECT, L"Không bao giờ bán/hủy món có tên chứa (cách nhau dấu phẩy):", P);
    Ctl(L"EDIT", L"", WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL, IDC_EDIT_PROTECT, P);
    Ctl(L"BUTTON", L"Xem túi đồ (thuộc tính từng món)", BS_PUSHBUTTON, IDC_BTN_DUMP, P);
    Label(IDC_LBL_SAFENOTE,
          L"Luôn bỏ qua: Võ Lâm Lệnh, thuốc đang tick ở tab THUỐC, món đang gắn phím tắt, đồ đang mặc.\r\n"
          L"Mỗi món: đọc lại đúng ô đó ngay trước khi gửi lệnh (phải giống hệt), làm từng món, chờ server xác nhận rồi mới làm món sau.\r\n"
          L"Tự DỪNG KHẨN CẤP khi: một món khác biến mất trong lúc bán/hủy; khớp quá 10 món hoặc quá 30% túi cùng lúc; "
          L"3 lần liên tiếp không xác nhận được; hủy quá 60 hoặc bán quá 120 món trong phiên. Bấm \"Bật lại\" sau khi kiểm tra.\r\n"
          L"Nhật ký mọi món đã bán/hủy: data\\banhuy_<tài khoản>.log", P);

    // ---- TIEN ICH > DI CHUYEN
    P = &g_pgUtil[7];
    Box(IDC_CHK_TELE, L"Truyền tống khi có VIP (Võ Lâm Lệnh) — miễn phí", P);
    Box(IDC_CHK_VLL, L"Tự dùng Võ Lâm Lệnh trong túi khi hết VIP (mỗi lần dùng mất 1 vật phẩm)", P);
    Label(IDC_LBL_MOVENOTE, L"Chỉ truyền tống khi nhân vật đang có trạng thái VIP (truyền tống miễn phí). Không có VIP thì luôn đi bộ bằng "
                            L"tự tìm đường của game, không bao giờ dùng truyền tống tốn tpoint. Rê chuột vào ô VIP bên trái để xem hạn.", P);

    // ---- TIEN ICH > CAI DAT GAME
    P = &g_pgUtil[8];
    {
        static const wchar_t* GSN[13] = { L"Ẩn hình ảnh người chơi khác", L"Ẩn tên người chơi khác", L"Tắt rung màn hình (chí mạng)",
            L"Tắt hiệu ứng tim đập khi máu thấp", L"Ẩn bảng theo dõi nhiệm vụ", L"Ẩn bảng thành viên tổ đội", L"Tắt nhạc nền",
            L"Tắt âm thanh hiệu ứng", L"Chặn người khác chat riêng", L"Chặn người khác kết bạn", L"Từ chối giao dịch",
            L"Từ chối mời vào bang", L"Từ chối mời vào tổ đội" };
        for (int i = 0; i < 13; ++i) Box(IDC_CHK_GS0 + i, GSN[i], P);
        Box(IDC_CHK_HPFX, L"Ẩn hiệu ứng sinh lực người khác", P);
        Box(IDC_CHK_CLOSEUI, L"Tự đóng cửa sổ tính năng hiện lên khi mới vào game (20 giây đầu, như bấm Esc)", P);
        Label(IDC_LBL_HIDELV, L"Khi ẩn cửa sổ game:", P);
        Combo(IDC_CMB_HIDELV, P, { L"Mức 1 – tắt vẽ hình, giữ nguyên nhịp game", L"Mức 2 – tắt vẽ hình + 15 khung hình/giây" });
        Label(IDC_LBL_GSNOTE, L"Các mục trên là cài đặt có sẵn của game (giống bảng Cài đặt hệ thống / phím tắt trong game), tự áp dụng mỗi lần vào game. "
                              L"Bỏ tick mục đã bật thì tool trả lại như cũ. So sánh các mức ẩn ở tab HIỆU NĂNG.", P);
    }

    // ---- HIEU NANG (tab chinh 5)
    P = &g_pagePerf;
    g_perf = Ctl(WC_LISTVIEWW, L"", LVS_REPORT | LVS_NOSORTHEADER | WS_BORDER, IDC_PERFLIST, P);
    ListView_SetExtendedListViewStyle(g_perf, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    AddColumn(g_perf, 0, L"Chỉ số", 190); AddColumn(g_perf, 1, L"Đang hiện", 120); AddColumn(g_perf, 2, L"Ẩn mức 1", 120); AddColumn(g_perf, 3, L"Ẩn mức 2", 120);
    {
        static const wchar_t* PR[] = { L"Thời gian đo", L"CPU (cửa sổ game)", L"Khung hình thực tế / đặt", L"Trễ nhịp bot (tb / lớn nhất)",
                                       L"Skill / phút", L"Giết / phút", L"Thời gian nhặt (tb)", L"Tốc độ đi (ô/giây)" };
        for (int i = 0; i < 8; ++i) LvInsert(g_perf, i, PR[i]);
    }
    Ctl(L"BUTTON", L"Đặt lại số đo", BS_PUSHBUTTON, IDC_BTN_PERFRESET, P);
    Label(IDC_LBL_PERFNOTE, L"Số đo của tài khoản đang chọn, tách theo trạng thái cửa sổ. Chạy mỗi chế độ vài phút ở cùng bãi rồi so sánh. "
                            L"Ẩn/hiện cửa sổ: chuột phải vào danh sách tài khoản.", P);

    // ---- TAI KHOAN
    P = &g_pageAcc;
    HWND at = Label(IDC_LBL_A_TITLE, L"TÀI KHOẢN ĐĂNG NHẬP GAME", P);
    SendMessageW(at, WM_SETFONT, (WPARAM)g_fontBold, TRUE);
    Label(IDC_LBL_A_USER, L"Tài khoản:", P); Edit(IDC_EDIT_A_USER, P);
    Label(IDC_LBL_A_PASS, L"Mật khẩu:", P); Edit(IDC_EDIT_A_PASS, P, ES_PASSWORD);
    Ctl(L"BUTTON", L"Hiện", BS_PUSHBUTTON, IDC_BTN_A_EYE, P);
    Label(IDC_LBL_A_SV, L"Máy chủ:", P); Edit(IDC_EDIT_A_SV, P, ES_NUMBER);
    Label(IDC_LBL_A_KENH, L"Kênh:", P);
    Combo(IDC_CMB_A_KENH, P, { L"Kênh 1", L"Kênh 2", L"Kênh 3", L"Kênh 4", L"Kênh 5", L"Kênh 6", L"Kênh 7", L"Kênh 8" });
    Label(IDC_LBL_A_NV, L"Nhân vật:", P); Edit(IDC_EDIT_A_NV, P, ES_READONLY);
    Ctl(L"BUTTON", L"Lưu", BS_PUSHBUTTON, IDC_BTN_A_SAVE, P);
    Ctl(L"BUTTON", L"ĐĂNG NHẬP", BS_PUSHBUTTON, IDC_BTN_A_LOGIN, P);
    Ctl(L"BUTTON", L"Hủy", BS_PUSHBUTTON, IDC_BTN_A_CANCEL, P);
    Label(IDC_LBL_A_NOTE, L"Nhân vật tự điền sau khi vào game thành công. Lần đầu (chưa có tên): tài khoản chỉ có 1 nhân vật thì tự chọn, "
                          L"nhiều nhân vật thì bạn chọn tay một lần.\r\n"
                          L"Lưu vào data\\accounts.xml; mật khẩu được mã hóa (DPAPI) — chỉ máy này, tài khoản Windows này đọc được.\r\n"
                          L"Chuột phải vào danh sách tài khoản: Đăng nhập / Cửa sổ mới.", P);

    // Cac tab nam duoi cung (tab chinh duoi tab con), moi control khac ve de len tren.
    SetWindowPos(g_tabSk, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SetWindowPos(g_tabTrain, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SetWindowPos(g_tabUtil, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SetWindowPos(g_tabMain, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    SetSel(IDC_CMB_R_KIND, 0); SetSel(IDC_CMB_R_Q, 1); SetSel(IDC_CMB_R_LOCK, 0); SetSel(IDC_CMB_R_LV, 0);
    SetSel(IDC_CMB_SUPTYPE, 0); SetText(IDC_EDIT_SUPPCT, L"70");
    UpdateRuleEditor();
    LoadUI();
    Layout();
}

// ------------------------------------------------------------ WndProc

static void AddRuleFromEditor(Acc* a) {
    Rule r;
    r.act = TabCtrl_GetCurSel(g_tabUtil) == 4 ? 0 : 1;          // trang BAN = ban, trang HUY = huy
    r.kind = CurSel(IDC_CMB_R_KIND);
    r.real = false;
    if (r.kind == 0) {
        r.q = CurSel(IDC_CMB_R_Q); if (r.q == 0) r.q = -1;
        std::wstring s = GetText(IDC_EDIT_R_S);
        r.s = s.empty() ? -1 : ToInt(s, -1);
        int lv = CurSel(IDC_CMB_R_LV); r.g = lv <= 0 ? -1 : lv * 10;
        for (int i = 0; i < NSECTS; ++i) if (Checked(IDC_CHK_SECT0 + i)) r.sects += (r.sects.empty() ? L"" : L",") + std::to_wstring(SECTS[i].id);
        for (int i = 0; i < NSLOTS; ++i) if (Checked(IDC_CHK_SLOT0 + i)) r.slots += (r.slots.empty() ? L"" : L",") + std::to_wstring(SLOTS[i].id);
        r.lock = CurSel(IDC_CMB_R_LOCK);
        if (r.q == 4 && a->cfg.floorQ)
            MessageBoxW(g_hwnd, L"Đang bật \"Bảo vệ đồ màu Tím trở lên\" ở tab AN TOÀN nên đồ Tím vẫn được giữ lại.", L"Lưu ý", MB_ICONINFORMATION);
    } else {
        std::wstring n;
        for (auto& x : SplitNames(GetText(IDC_EDIT_R_NAMES))) n += (n.empty() ? L"" : L",") + x;
        r.names = n;
    }
    if (!RuleValid(r)) {
        MessageBoxW(g_hwnd, r.kind == 0 ? L"Quy tắc trang bị phải có ít nhất một điều kiện (màu, số sao, cấp dùng, môn phái, loại hoặc khóa)."
                                        : L"Quy tắc vật phẩm phải có ít nhất một tên.", L"Không thêm được", MB_ICONWARNING);
        return;
    }
    a->cfg.rules.push_back(r);
    SaveConfig(*a);
    RefreshRules(a->cfg);
    if (a->connected && a->st.inGame) SendUtil(a);
    AddLog(a, std::wstring(r.act == 0 ? L"Bán" : L"Hủy") + L" - Thêm quy tắc (chạy thử): " + RuleText(r));
}

static LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        g_hwnd = h;
        CreateUI();
        SetTimer(h, 1, 1000, nullptr);      // tick
        SetTimer(h, 2, 3000, nullptr);      // tim cua so moi
        LoadXmlRows();
        Discover();
        return 0;
    case WM_SIZE:
        if (g_acc) Layout();
        return 0;
    case WM_LBUTTONUP:
        if (g_dragRow >= 0) {
            ReleaseCapture();
            int from = g_dragRow; g_dragRow = -1;
            Acc* a = Sel(); int sk = TabCtrl_GetCurSel(g_tabSk);
            if (!a || sk >= 4) return 0;
            POINT pt; GetCursorPos(&pt); ScreenToClient(g_skOrder, &pt);
            LVHITTESTINFO hi = {}; hi.pt = pt;
            int to = (int)SendMessageW(g_skOrder, LVM_HITTEST, 0, (LPARAM)&hi);
            auto ids = IdList(a->cfg.sets[g_skSet].g[sk]);
            if (to < 0) {                                    // tha ngoai dong: tren dong dau -> dau danh sach, con lai -> cuoi
                RECT r0 = {}; r0.left = LVIR_BOUNDS;
                bool top = ids.empty() || !SendMessageW(g_skOrder, LVM_GETITEMRECT, 0, (LPARAM)&r0) || pt.y < r0.top;
                to = top ? 0 : (int)ids.size() - 1;
            }
            if (from >= 0 && from < (int)ids.size() && to >= 0 && to < (int)ids.size() && from != to) {
                std::wstring x = ids[from]; ids.erase(ids.begin() + from); ids.insert(ids.begin() + to, x);
                a->cfg.sets[g_skSet].g[sk] = JoinIds(ids);
                SkillSetChanged(a);
                ListView_SetItemState(g_skOrder, to, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            }
            return 0;
        }
        break;
    case WM_GETMINMAXINFO:
        ((MINMAXINFO*)lp)->ptMinTrackSize = { 1040, 820 };
        return 0;
    case WM_APP + 50:                          // tick tai khoan nhung khong mo duoc client: bo tick lai
        if ((int)wp < (int)g_accs.size()) SyncTick(g_accs[wp].get());
        return 0;
    case WM_TIMER:
        if (wp == 1) Tick(); else Discover();
        return 0;
    case WM_LINK_LINE: {
        std::unique_ptr<LinkMsg> m((LinkMsg*)lp);
        if (Acc* a = Find(m->acc)) OnLine(a, m->line);
        return 0;
    }
    case WM_LINK_CONN: {
        std::unique_ptr<LinkMsg> m((LinkMsg*)lp);
        if (Acc* a = Find(m->acc)) {
            if (wp == 2) { AddLog(a, L"vlcmhost đang bị tool khác (vlcmctl?) kết nối — tắt tool đó, panel sẽ tự nối lại"); return 0; }
            a->connected = wp != 0;
            if (a->connected) a->everConnected = true;
            a->skillsAsked = false; a->pid = 0;
            a->pending.clear();
            a->utilSent = false;
            a->adopted = false; a->quitAt = 0; a->pauseAt = 0;
            if (a->connected) {
                a->relaunchAt = 0; a->gameDiscoAt = 0;
                AddLog(a, L"Đã kết nối vlcmhost"); a->lastStatus = 0; a->lastStart = GetTickCount();
                if (!a->manualOff && !a->active) MarkActive(a);          // client dang chay: tu tick
            } else {
                a->st.inGame = false; a->hidden = false; AddLog(a, L"Mất kết nối vlcmhost");
                DWORD now = GetTickCount();
                a->relaunches.erase(std::remove_if(a->relaunches.begin(), a->relaunches.end(), [now](DWORD t) { return now - t > 600000; }), a->relaunches.end());
                if (a->active && !a->hostClosing && a->inXml) {            // tat bat thuong: giu tick, mo lai (khong gioi han so lan)
                    DWORD wait = a->relaunches.size() >= 3 ? 60000 : 8000;  // tat lien tuc (>= 3 lan / 10 phut): cho 60s cho do don dap
                    a->relaunchAt = now + wait;
                    AddLog(a, L"Client tắt bất thường — tự mở lại sau " + std::to_wstring(wait / 1000) + L" giây");
                } else {
                    a->active = false; a->manualOff = false; SyncTick(a);    // client da tat: bo tick (giu cac o chuc nang de tick lai la chay tiep)
                }
            }
            a->hostClosing = false;
            RefreshRow(a); RefreshConnInfo();
            if (a == Sel()) RefreshTrainState();
        }
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        if (g_pbPane >= 0) { FillRect(dc, &g_pbRect, GetSysColorBrush(COLOR_WINDOW)); FrameRect(dc, &g_pbRect, GetSysColorBrush(COLOR_BTNSHADOW)); }
        EndPaint(h, &ps);
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)wp; HWND c = (HWND)lp;
        if (c == g_log) { SetTextColor(dc, RGB(230, 230, 230)); SetBkColor(dc, RGB(43, 43, 43)); return (LRESULT)g_brLog; }
        if (c == Item(IDC_LBL_CTRL)) { SetTextColor(dc, RGB(200, 40, 40)); SetBkMode(dc, TRANSPARENT); return (LRESULT)GetSysColorBrush(COLOR_BTNFACE); }
        if (c == Item(IDC_LBL_VIP)) {
            Acc* a = Sel();
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, a && a->st.inGame && a->st.vip ? RGB(0, 140, 60) : RGB(120, 120, 120));
            return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
        }
        if (c == Item(IDC_LBL_BHSTATE)) {
            Acc* a = Sel();
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, a && a->st.bh == L"stop" ? RGB(200, 0, 0) : RGB(40, 40, 40));
            return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
        }
        if (IsOnPage(c)) {                     // chu tren nen trang cua tab
            SetBkMode(dc, TRANSPARENT);
            return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
        }
        for (int i = 0; i < NPB; ++i) if (c == g_pbState[i]) {
            std::wstring t = PbStateText(Sel(), i);
            SetBkMode(dc, TRANSPARENT);
            bool blue = t == L"Đang làm", green = t == L"Đã xong", yel = t == L"Đang chờ" || t == L"Bỏ qua";
            SetTextColor(dc, blue || green ? RGB(255, 255, 255) : RGB(40, 40, 40));
            return (LRESULT)(blue ? g_brBlue : green ? g_brGreen : yel ? g_brYellow : g_brGray);
        }
        if (c == g_trainState) {
            Acc* a = Sel();
            std::wstring t = TrainStateText(a);
            bool on = t == L"Đang làm", wait = !on && t != L"Chưa làm";
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, on ? RGB(255, 255, 255) : RGB(40, 40, 40));
            return (LRESULT)(wait ? g_brYellow : on ? g_brBlue : g_brGray);
        }
        break;
    }
    case WM_NOTIFY: {
        NMHDR* n = (NMHDR*)lp;
        if ((n->idFrom == IDC_TAB_MAIN || n->idFrom == IDC_TAB_TRAIN || n->idFrom == IDC_TAB_UTIL) && n->code == TCN_SELCHANGE) {
            UpdatePages(); if (n->idFrom == IDC_TAB_MAIN) RefreshPerf(); return 0;
        }
        if (n->idFrom == IDC_TAB_SK && n->code == TCN_SELCHANGE) { UpdatePages(); RefreshSkillPage(); return 0; }
        if (n->idFrom == IDC_SKLIST && n->code == LVN_BEGINDRAG) {
            g_dragRow = ((NMLISTVIEW*)lp)->iItem;
            SetCapture(g_hwnd); SetCursor(LoadCursorW(nullptr, IDC_SIZENS));
            return 0;
        }
        if (n->idFrom == IDC_ACCLIST && n->code == NM_RCLICK) {
            NMITEMACTIVATE* ia = (NMITEMACTIVATE*)lp;
            if (ia->iItem >= 0) ListView_SetItemState(g_acc, ia->iItem, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            HMENU m = CreatePopupMenu();
            AppendMenuW(m, MF_STRING | (ia->iItem >= 0 ? 0 : MF_GRAYED), IDM_LOGIN, L"Đăng nhập");
            AppendMenuW(m, MF_STRING, IDM_NEWWIN, L"Cửa sổ mới");
            AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
            Acc* ra = ia->iItem >= 0 && ia->iItem < (int)g_accs.size() ? g_accs[ia->iItem].get() : nullptr;
            AppendMenuW(m, MF_STRING | (ra && ra->connected && !ra->hidden ? 0 : MF_GRAYED), IDM_HIDE, L"Ẩn cửa sổ");
            AppendMenuW(m, MF_STRING | (ra && ra->connected ? 0 : MF_GRAYED), IDM_SHOW, L"Hiện cửa sổ");
            AppendMenuW(m, MF_STRING, IDM_HIDEALL, L"Ẩn tất cả");
            AppendMenuW(m, MF_STRING, IDM_SHOWALL, L"Hiện tất cả");
            AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(m, MF_STRING | (ra && ra->connected ? 0 : MF_GRAYED), IDM_QUIT, L"Tắt cửa sổ");
            AppendMenuW(m, MF_STRING, IDM_QUITALL, L"Tắt tất cả");
            POINT pt; GetCursorPos(&pt);
            TrackPopupMenu(m, TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_hwnd, nullptr);
            DestroyMenu(m);
            return 0;
        }
        if (n->idFrom == IDC_ACCLIST && n->code == LVN_ITEMCHANGED) {
            NMLISTVIEW* v = (NMLISTVIEW*)lp;
            if (!g_syncTick && (v->uChanged & LVIF_STATE) && v->iItem >= 0 && v->iItem < (int)g_accs.size()
                && (v->uOldState & LVIS_STATEIMAGEMASK) && ((v->uNewState ^ v->uOldState) & LVIS_STATEIMAGEMASK)) {
                Acc* t = g_accs[v->iItem].get();
                bool on = ListView_GetCheckState(g_acc, v->iItem) != 0;
                if (on && !t->active) {                  // tick = mo client (dang chay thi bat lai chuc nang)
                    if (t->connected) { MarkActive(t); AddLog(t, L"Bật lại các chức năng"); }
                    else if (!LoginAcc(t)) PostMessageW(g_hwnd, WM_APP + 50, (WPARAM)v->iItem, 0);   // khong mo duoc: bo tick (sau khi thong bao xong)
                } else if (!on && t->active) {           // bo tick = dung chuc nang, khong tat client
                    t->active = false; t->manualOff = t->connected; t->relaunchAt = 0;
                    SetEnabled(t, false);
                    if (t->connected && t->st.state == L"pb") SendCmd(t, L"pbstop", L"pb_stop");
                    AddLog(t, L"Đã dừng các chức năng (client vẫn mở)");
                    if (t == Sel()) RefreshTrainState();
                }
            }
            if ((v->uChanged & LVIF_STATE) && (v->uNewState & LVIS_SELECTED) && !(v->uOldState & LVIS_SELECTED)) {
                g_sel = v->iItem;
                g_newAcc = false;
                LoadUI();
            }
            return 0;
        }
        bool checkFlip = false;
        NMLISTVIEW* v = (NMLISTVIEW*)lp;
        if (n->code == LVN_ITEMCHANGED && !g_loading)
            checkFlip = (v->uChanged & LVIF_STATE) && ((v->uNewState ^ v->uOldState) & LVIS_STATEIMAGEMASK) && v->iItem >= 0;
        Acc* a = Sel();
        if (!checkFlip || !a) break;
        if (n->idFrom == IDC_PTLIST && v->iItem < (int)a->cfg.pts.size()) {
            a->cfg.pts[v->iItem].on = ListView_GetCheckState(g_pts, v->iItem) != 0;
            SaveConfig(*a);
            return 0;
        }
        if (n->idFrom == IDC_DRUGLIST) {
            std::wstring ids;
            for (int i = 0; i < NDRUGS; ++i) if (ListView_GetCheckState(g_drugs, i)) ids += (ids.empty() ? L"" : L",") + std::to_wstring(DRUGS[i].id);
            a->cfg.drugIds = ids;
            UtilChanged();
            return 0;
        }
        if ((n->idFrom == IDC_RULELIST || n->idFrom == IDC_RULELIST2)) {
            int act = n->idFrom == IDC_RULELIST ? 0 : 1;
            HWND lv = act == 0 ? g_rules : g_rules2;
            auto rows = RuleRows(a->cfg, act);
            if (v->iItem >= (int)rows.size()) return 0;
            Rule& r = a->cfg.rules[rows[v->iItem]];
            bool on = ListView_GetCheckState(lv, v->iItem) != 0;
            if (on == r.real) return 0;
            if (on) {
                std::wstring q = L"Tắt chạy thử cho quy tắc " + std::to_wstring(v->iItem + 1) + L"?\r\n\r\n" + RuleText(r) +
                                 L"\r\n\r\nBot sẽ " + (r.act == 0 ? L"BÁN" : L"HỦY") + L" THẬT các món khớp quy tắc này. Hãy xem log \"Chạy thử\" trước để chắc chắn.";
                if (MessageBoxW(g_hwnd, q.c_str(), L"Xác nhận làm thật", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) {
                    RefreshRules(a->cfg); return 0;
                }
            }
            r.real = on;
            SaveConfig(*a);
            RefreshRules(a->cfg);
            if (a->connected && a->st.inGame) SendUtil(a);
            AddLog(a, std::wstring(act == 0 ? L"Bán" : L"Hủy") + L" - Quy tắc " + std::to_wstring(v->iItem + 1) + (on ? L": LÀM THẬT" : L": chạy thử"));
            return 0;
        }
        break;
    }
    case WM_COMMAND: {
        int id = LOWORD(wp), code = HIWORD(wp);
        Acc* a = Sel();
        if (id >= IDC_CHK_GS0 && id < IDC_CHK_GS0 + 13) { if (code == BN_CLICKED) UtilChanged(); return 0; }
        if (id >= PBX(0, 0) && id < PBX(NPB, 0)) {          // khung cai dat pho ban
            if ((code == EN_CHANGE || code == BN_CLICKED || code == CBN_SELCHANGE) && !g_loading && a) { ReadUI(a->cfg); SaveConfig(*a); RefreshTrainState(); }
            if (code == EN_SETFOCUS) PostMessageW((HWND)lp, EM_SETSEL, 0, -1);
            return 0;
        }
        if (id == IDC_PBP_CLOSE) { g_pbPane = -1; UpdatePages(); return 0; }
        switch (id) {
        case IDC_CHK_TRAIN:
            if (!g_loading && a) SetEnabled(a, Checked(IDC_CHK_TRAIN));
            return 0;
        case IDC_CHK_PB0: case IDC_CHK_PB0 + 1: case IDC_CHK_PB0 + 2: case IDC_CHK_PB0 + 3: case IDC_CHK_PB0 + 4:
            if (!g_loading && a) {
                int i = id - IDC_CHK_PB0;
                a->cfg.pbOn[i] = Checked(id);
                a->pbSkip[i].clear();
                AddLog(a, std::wstring(L"Phó bản ") + PB_NAME[i] + (a->cfg.pbOn[i] ? L" - Bật" : L" - Tắt"));
                if (a->connected && a->st.state == L"pb") {     // dang chay: chi cap nhat danh sach, pho ban dang lam lam het luot
                    SendCmd(a, L"pblist", L"pb_list " + PbArgs(a));
                    a->pbStartAt = GetTickCount();
                } else a->pbStartAt = GetTickCount() - 7000;
                RefreshTrainState();
            }
            return 0;
        case IDC_BTN_PBG0: case IDC_BTN_PBG0 + 1: case IDC_BTN_PBG0 + 2: case IDC_BTN_PBG0 + 3: case IDC_BTN_PBG0 + 4:
            g_pbPane = g_pbPane == id - IDC_BTN_PBG0 ? -1 : id - IDC_BTN_PBG0; UpdatePages();
            return 0;
        case IDC_EDIT_PBRUNS0: case IDC_EDIT_PBRUNS0 + 1: case IDC_EDIT_PBRUNS0 + 2: case IDC_EDIT_PBRUNS0 + 3: case IDC_EDIT_PBRUNS0 + 4:
        case IDC_EDIT_PBREV0: case IDC_EDIT_PBREV0 + 1: case IDC_EDIT_PBREV0 + 2: case IDC_EDIT_PBREV0 + 3: case IDC_EDIT_PBREV0 + 4:
        case IDC_EDIT_TQMINR: case IDC_EDIT_MCMIN: case IDC_EDIT_MCLZ:
            if (code == EN_CHANGE && !g_loading && a) { ReadUI(a->cfg); SaveConfig(*a); RefreshTrainState(); }
            if (code == EN_SETFOCUS) PostMessageW((HWND)lp, EM_SETSEL, 0, -1);
            return 0;
        case IDC_CHK_DTJUMP: case IDC_CHK_PTJUMP: case IDC_CHK_PTBOW: case IDC_CHK_MCSKIP:
            if (code == BN_CLICKED && !g_loading && a) { ReadUI(a->cfg); SaveConfig(*a); }
            return 0;
        case IDC_CMB_MCFARM: case IDC_CMB_DTJMAX: case IDC_CMB_MCBY:
            if (code == CBN_SELCHANGE && !g_loading && a) { ReadUI(a->cfg); SaveConfig(*a); }
            return 0;
        case IDC_BTN_GEAR:
            g_pbPane = -1; TabCtrl_SetCurSel(g_tabMain, 1); TabCtrl_SetCurSel(g_tabTrain, 1); UpdatePages();
            return 0;
        case IDM_LOGIN:
            LoginAcc(a);
            return 0;
        case IDM_HIDE: if (a) SetHidden(a, true); return 0;
        case IDM_SHOW: if (a) SetHidden(a, false); return 0;
        case IDM_HIDEALL: for (auto& o : g_accs) if (o->connected) SetHidden(o.get(), true); return 0;
        case IDM_SHOWALL: for (auto& o : g_accs) if (o->connected) SetHidden(o.get(), false); return 0;
        case IDM_QUIT:
            if (a && a->connected && MessageBoxW(g_hwnd, (L"Tắt cửa sổ game của " + a->id + L"?").c_str(), L"Tắt cửa sổ", MB_YESNO | MB_ICONQUESTION) == IDYES)
                QuitAcc(a);
            return 0;
        case IDM_QUITALL: {
            int n = 0; for (auto& o : g_accs) if (o->connected) ++n;
            if (n == 0) return 0;
            if (MessageBoxW(g_hwnd, (L"Tắt " + std::to_wstring(n) + L" cửa sổ game đang mở?").c_str(), L"Tắt tất cả", MB_YESNO | MB_ICONQUESTION) == IDYES)
                for (auto& o : g_accs) if (o->connected) QuitAcc(o.get());
            return 0;
        }
        case IDC_BTN_PERFRESET:
            if (a) { SendCmd(a, L"perfreset", L"perf_reset"); for (int i = 0; i < 3; ++i) a->cpuMs[i] = a->cpuWall[i] = 0; AddLog(a, L"Đã đặt lại số đo hiệu năng"); }
            return 0;
        case IDC_CMB_SKSET:
            if (code == CBN_SELCHANGE) { g_skSet = CurSel(IDC_CMB_SKSET); RefreshSkillPage(); }
            return 0;
        case IDC_CMB_TRAINSET:
            if (code == CBN_SELCHANGE) ConfigChanged();
            return 0;
        case IDC_BTN_SKADD: {
            int sk = TabCtrl_GetCurSel(g_tabSk); std::wstring id = ComboId(IDC_CMB_SKADD);
            if (!a || sk >= 4 || id.empty()) return 0;
            auto ids = IdList(a->cfg.sets[g_skSet].g[sk]); ids.push_back(id);
            a->cfg.sets[g_skSet].g[sk] = JoinIds(ids);
            SkillSetChanged(a);
            return 0;
        }
        case IDC_BTN_SKUP: case IDC_BTN_SKDOWN: case IDC_BTN_SKDEL: {
            int sk = TabCtrl_GetCurSel(g_tabSk), row = ListView_GetNextItem(g_skOrder, -1, LVNI_SELECTED);
            if (!a || sk >= 4 || row < 0) return 0;
            auto ids = IdList(a->cfg.sets[g_skSet].g[sk]);
            if (row >= (int)ids.size()) return 0;
            int to = row;
            if (id == IDC_BTN_SKDEL) ids.erase(ids.begin() + row);
            else { to = id == IDC_BTN_SKUP ? row - 1 : row + 1; if (to < 0 || to >= (int)ids.size()) return 0; std::swap(ids[row], ids[to]); }
            a->cfg.sets[g_skSet].g[sk] = JoinIds(ids);
            SkillSetChanged(a);
            if (id != IDC_BTN_SKDEL) ListView_SetItemState(g_skOrder, to, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            return 0;
        }
        case IDC_CMB_SUPTYPE:
            if (code == CBN_SELCHANGE) EnableWindow(Item(IDC_EDIT_SUPPCT), CurSel(IDC_CMB_SUPTYPE) == 0);
            return 0;
        case IDC_BTN_SUPADD: {
            std::wstring sid = ComboId(IDC_CMB_SUPSKILL);
            if (!a || sid.empty()) return 0;
            auto rows = SupRows(a->cfg.sets[g_skSet]);
            bool heal = CurSel(IDC_CMB_SUPTYPE) == 0;
            int pct = (std::min)(95, (std::max)(5, ToInt(GetText(IDC_EDIT_SUPPCT), 70)));
            rows.push_back({ heal, sid, pct });
            SetSupRows(a->cfg.sets[g_skSet], rows);
            SkillSetChanged(a);
            return 0;
        }
        case IDC_BTN_SUPDEL: {
            int row = ListView_GetNextItem(g_supList, -1, LVNI_SELECTED);
            if (!a || row < 0) return 0;
            auto rows = SupRows(a->cfg.sets[g_skSet]);
            if (row < (int)rows.size()) { rows.erase(rows.begin() + row); SetSupRows(a->cfg.sets[g_skSet], rows); SkillSetChanged(a); }
            return 0;
        }
        case IDC_EDIT_SUPMP:
            if (code == EN_KILLFOCUS && a && !g_loading) {
                a->cfg.sets[g_skSet].supmp = (std::min)(95, (std::max)(0, ToInt(GetText(IDC_EDIT_SUPMP), 30)));
                SkillSetChanged(a);
            }
            if (code == EN_SETFOCUS) PostMessageW((HWND)lp, EM_SETSEL, 0, -1);
            return 0;
        case IDC_CHK_HPFX: case IDC_CHK_CLOSEUI:
            if (code == BN_CLICKED) UtilChanged();
            return 0;
        case IDC_CMB_HIDELV:
            if (code == CBN_SELCHANGE) {
                UtilChanged();
                if (a && a->hidden && a->connected) SendCmd(a, L"render", std::wstring(L"render mode=") + (a->cfg.hideLevel == 2 ? L"hide2" : L"hide1"));
            }
            return 0;
        case IDM_NEWWIN:
            g_newAcc = true;
            g_pbPane = -1; TabCtrl_SetCurSel(g_tabMain, 3); UpdatePages();
            LoadAccForm();
            SetFocus(Item(IDC_EDIT_A_USER));
            return 0;
        case IDC_BTN_A_EYE:
            SetPassVisible(!g_showPass);
            return 0;
        case IDC_BTN_A_SAVE:
            SaveAccForm(nullptr);
            return 0;
        case IDC_BTN_A_LOGIN: {
            std::wstring u;
            if (!SaveAccForm(&u)) return 0;
            for (auto& p : g_accs) if (_wcsicmp(p->id.c_str(), u.c_str()) == 0) LoginAcc(p.get());
            return 0;
        }
        case IDC_BTN_A_CANCEL:
            g_newAcc = false; LoadAccForm();
            return 0;
        case IDC_BTN_BHSTOP:
            if (!a) return 0;
            a->cfg.bhOn = false; SaveConfig(*a);                  // tat han trong cai dat
            g_loading = true; Check(IDC_CHK_BH, false); g_loading = false;
            SendCmd(a, L"bhstop", L"bh_stop");
            SendUtil(a);
            AddLog(a, L"Bán/Hủy - Đã bấm DỪNG KHẨN CẤP (đã bỏ tick \"Bật bán / hủy\")");
            return 0;
        case IDC_BTN_BHRESET:
            if (!a) return 0;
            SendCmd(a, L"bhreset", L"bh_reset");
            return 0;
        case IDC_BTN_STATRESET:
            if (a) SendCmd(a, L"statreset", L"stats_reset");
            return 0;
        case IDC_BTN_DUMP:
            if (a && a->connected) SendCmd(a, L"dump", L"bag_dump"); else if (a) AddLog(a, L"Chưa kết nối vlcmhost");
            return 0;
        case IDC_BTN_ADDPT:
            if (!a) return 0;
            if (!a->connected) { AddLog(a, L"Chưa kết nối vlcmhost"); return 0; }
            SendCmd(a, L"where", L"where");
            return 0;
        case IDC_BTN_DELPT: {
            int row = ListView_GetNextItem(g_pts, -1, LVNI_SELECTED);
            if (a && row >= 0 && row < (int)a->cfg.pts.size()) {
                a->cfg.pts.erase(a->cfg.pts.begin() + row);
                a->curPt = 0;
                SaveConfig(*a); RefreshPoints();
            }
            return 0;
        }
        case IDC_BTN_SETR: {
            int row = ListView_GetNextItem(g_pts, -1, LVNI_SELECTED);
            if (a && row >= 0 && row < (int)a->cfg.pts.size()) {
                a->cfg.pts[row].r = (std::max)(1, ToInt(GetText(IDC_EDIT_R), 5));
                SaveConfig(*a); RefreshPoints();
                if (a->cfg.enabled) a->restartAt = GetTickCount() + 1500;
            }
            return 0;
        }
        case IDC_BTN_R_ADD:
            if (a) AddRuleFromEditor(a);
            return 0;
        case IDC_BTN_R_DEL: {
            int act = TabCtrl_GetCurSel(g_tabUtil) == 4 ? 0 : 1;
            int row = ListView_GetNextItem(act == 0 ? g_rules : g_rules2, -1, LVNI_SELECTED);
            if (!a) return 0;
            auto rows = RuleRows(a->cfg, act);
            if (row >= 0 && row < (int)rows.size()) {
                a->cfg.rules.erase(a->cfg.rules.begin() + rows[row]);
                SaveConfig(*a); RefreshRules(a->cfg);
                if (a->connected && a->st.inGame) SendUtil(a);
                AddLog(a, std::wstring(act == 0 ? L"Bán" : L"Hủy") + L" - Đã xóa quy tắc " + std::to_wstring(row + 1));
            }
            return 0;
        }
        case IDC_CMB_R_KIND:
            if (code == CBN_SELCHANGE) UpdateRuleEditor();
            return 0;
        case IDC_BTN_APPLYALL:
            if (!a) return 0;
            for (auto& o : g_accs) {
                if (o.get() == a) continue;
                Config c = a->cfg;
                c.pts = o->cfg.pts; c.enabled = o->cfg.enabled;         // giu toa do va trang thai rieng
                o->cfg = c;
                SaveConfig(*o);
                if (o->connected && o->st.inGame) SendUtil(o.get());
                if (o->cfg.enabled && o->connected && o->st.inGame) o->restartAt = GetTickCount() + 1500;
            }
            AddLog(a, L"Đã áp dụng cài đặt cho " + std::to_wstring(g_accs.size() - 1) + L" acc khác");
            return 0;
        case IDC_CHK_T1: case IDC_CHK_T2: case IDC_CHK_T3: case IDC_CHK_REST: case IDC_CHK_SWTIME: case IDC_CHK_SWDEATH:
            if (code == BN_CLICKED) ConfigChanged();
            return 0;
        case IDC_CHK_FLOORQ:
            if (code == BN_CLICKED && !g_loading && !Checked(IDC_CHK_FLOORQ)) {
                if (MessageBoxW(g_hwnd, L"Bỏ bảo vệ đồ màu Tím trở lên?\r\n\r\nSau đó quy tắc bán/hủy có thể đụng tới đồ Tím / Vàng.",
                                L"Cảnh báo", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) { Check(IDC_CHK_FLOORQ, true); return 0; }
            }
            if (code == BN_CLICKED) UtilChanged();
            return 0;
        case IDC_CHK_REPAIR: case IDC_CHK_BH: case IDC_CHK_TELE: case IDC_CHK_VLL:
            if (code == BN_CLICKED) UtilChanged();
            return 0;
        case IDC_CMB_SKMODE: case IDC_CMB_PKMODE:
            if (code == CBN_SELCHANGE) ConfigChanged();
            return 0;
        case IDC_CMB_POTMODE: case IDC_CMB_RTYPE:
            if (code == CBN_SELCHANGE) UtilChanged();
            return 0;
        case IDC_EDIT_PKLIST:
            if (code == EN_CHANGE) ConfigChanged();
            return 0;
        case IDC_EDIT_PROTECT:
            if (code == EN_KILLFOCUS) UtilChanged();              // gui khi roi o (tranh gui moi phim)
            return 0;
        case IDC_BTN_GETSKILLS:
            if (a && a->connected) SendCmd(a, L"skills", L"skills"); else if (a) AddLog(a, L"Chưa kết nối vlcmhost");
            return 0;
        case IDC_BTN_GETBAG:
            if (a && a->connected) SendCmd(a, L"bag", L"bag_names"); else if (a) AddLog(a, L"Chưa kết nối vlcmhost");
            return 0;
        case IDC_BTN_PKDEFAULT:
            SetText(IDC_EDIT_PKLIST, DEFAULT_PICKLIST);
            ConfigChanged();                   // o nhieu dong khong bao EN_CHANGE khi dat chu bang code
            return 0;
        case IDC_BTN_ADDBAG: {
            HWND lb = Item(IDC_BAGLIST);
            int n = (int)SendMessageW(lb, LB_GETSELCOUNT, 0, 0);
            if (n <= 0) return 0;
            std::vector<int> sel((size_t)n);
            SendMessageW(lb, LB_GETSELITEMS, n, (LPARAM)sel.data());
            std::wstring list = GetText(IDC_EDIT_PKLIST);
            for (int idx : sel) {
                int len = (int)SendMessageW(lb, LB_GETTEXTLEN, idx, 0);
                std::wstring t((size_t)len, L'\0');
                SendMessageW(lb, LB_GETTEXT, idx, (LPARAM)&t[0]);
                if (list.find(L"[" + t + L"]") != std::wstring::npos) continue;
                if (!list.empty() && list.back() != L',') list += L",";
                list += L"[" + t + L"]";
            }
            SetText(IDC_EDIT_PKLIST, list);
            ConfigChanged();
            return 0;
        }
        case IDC_EDIT_KEYS: case IDC_EDIT_RESTN: case IDC_EDIT_RESTMIN: case IDC_EDIT_SWMIN: case IDC_EDIT_R:
            if (code == EN_CHANGE && id != IDC_EDIT_R) ConfigChanged();
            if (code == EN_SETFOCUS) PostMessageW((HWND)lp, EM_SETSEL, 0, -1);    // bam vao la chon het, go de thay
            return 0;
        case IDC_EDIT_HPKEY: case IDC_EDIT_MPKEY: case IDC_EDIT_HP: case IDC_EDIT_MP:
        case IDC_EDIT_FREEE: case IDC_EDIT_FREEI: case IDC_EDIT_FLOORS:
            if (code == EN_CHANGE) UtilChanged();
            if (code == EN_SETFOCUS) PostMessageW((HWND)lp, EM_SETSEL, 0, -1);
            return 0;
        case IDC_EDIT_R_S: case IDC_EDIT_R_G: case IDC_EDIT_R_P: case IDC_EDIT_R_POS:
            if (code == EN_SETFOCUS) PostMessageW((HWND)lp, EM_SETSEL, 0, -1);
            return 0;
        }
        break;
    }
    case WM_DESTROY:
        for (auto& a : g_accs) if (a->hidden) SetHidden(a.get(), false);     // dong panel: tra cua so game ve man hinh
        for (auto& a : g_accs) a->link.reset();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int show) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; argv && i + 1 < argc; ++i) {
        if (std::wstring(argv[i]) == L"--hosts") {
            std::wstring s = argv[i + 1];
            size_t p = 0;
            while (p <= s.size()) {
                size_t e = s.find(L',', p); if (e == std::wstring::npos) e = s.size();
                if (e > p) g_argHosts.push_back(s.substr(p, e - p));
                p = e + 1;
            }
        }
    }
    if (argv) LocalFree(argv);

    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES | ICC_STANDARD_CLASSES | ICC_BAR_CLASSES };
    InitCommonControlsEx(&icc);
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"VlcmPanel";
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);
    HWND h = CreateWindowExW(0, L"VlcmPanel", L"VLCM Panel - Train", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                             CW_USEDEFAULT, CW_USEDEFAULT, 1100, 860, nullptr, nullptr, inst, nullptr);
    ShowWindow(h, show);
    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0)) {
        if (IsDialogMessageW(h, &m)) continue;                 // Tab giua cac o nhap
        TranslateMessage(&m); DispatchMessageW(&m);
    }
    return 0;
}
