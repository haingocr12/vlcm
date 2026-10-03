// vlcmhost.cpp — Host Flash (C++/ATL) tai dung luong cua vlcmcliet.exe
// Build: Visual Studio, project Win32 (x86), co ATL. Link: winhttp.lib crypt32.lib
// Dat Flash.ocx vao  data\Flash.ocx  canh exe. Nhung file vlcmhost.manifest.
//
// Luong (xac minh bang Fiddler + Wireshark + dich nguoc TGame.swf, 24/09/2026):
//   HTTP : GetToken -> login -> gamepage.asp -> Movie = parameters.game + query string
//   Flash tu lam phan socket: 9000 (10011 auth/sign) -> 9002 (10105, 10019 md5, ping 10002/10003)
// F5 = dang nhap lai tu dau + nap lai game.
// (FlashHost.h khong dung o ban ATL nay — co the xoa.)
//
// Tu chon kenh + nhan vat (27/09/2026):
//   Movie = VlcmLoader.swf canh exe; link TGameLoader dua vao qua FlashBridge (cfg.url).
//   VlcmLoader nap TGameLoader ben trong no, chon kenh <kenh> va nhan vat <nv> cua accounts.xml.
//   Khong co VlcmLoader.swf -> nap thang TGameLoader nhu truoc (chon tay).
//   FlashBridge la sink su kien DUY NHAT: xu ly ca loi goi cua loader (vlcm_*) lan cua game
//   (console.log, setTips, showPopup, window.open). FlashCallSink.h khong dung nua - no dung
//   sai DIID {D27CDB71-...} nen Attach luon that bai ("khong tim thay _IShockwaveFlashEvents"
//   trong log); DIID dung la {D27CDB6D-...}.
//
// Train + tool dieu khien ngoai (28/09/2026):
//   Moi cua so mo named pipe \\.\pipe\vlcmhost-<tai khoan> (PipeServer.h). vlcmctl.exe gui lenh
//   (where, status, train_start ..., train_stop), host chuyen vao VlcmLoader.swf qua vlcm_command
//   tren luong UI, tra ket qua "> ..."; su kien cua loader duoc day ra dang "! <ten> <chi tiet>".
//   Lenh rieng cua host: ping, reload (= F5).

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <atlbase.h>
#include <atlwin.h>
#include <atlcom.h>
#include <atlctl.h>          // CAxWindow
#include <winhttp.h>
#include <string>
#include <regex>
#include <vector>
#include <memory>
#include <cstring>
#include <cwctype>
#include <shellapi.h>        // ShellExecuteW
#include "FlashBridge.h"     // sink su kien Flash + cau noi ExternalInterface voi VlcmLoader.swf
#include "AccountStore.h"    // doc data\accounts.xml
#include "PipeServer.h"      // kenh dieu khien cho vlcmctl.exe
#pragma comment(lib, "winhttp.lib")

// wstring -> UTF-8 (cho body JSON)
static std::string HttpUtf8(const std::wstring& w){
    if(w.empty())return{};
    int n=WideCharToMultiByte(CP_UTF8,0,w.data(),(int)w.size(),0,0,0,0);
    std::string s(n,0); WideCharToMultiByte(CP_UTF8,0,w.data(),(int)w.size(),&s[0],n,0,0);
    return s;
}

// Ghi log ra file vlcmhost_debug.log CANH EXE (duong dan tuyet doi)
#include <fstream>
static std::wstring ExeDir(){
    wchar_t buf[MAX_PATH]; GetModuleFileNameW(NULL,buf,MAX_PATH);
    std::wstring p=buf; size_t s=p.find_last_of(L"\\/");
    if(s!=std::wstring::npos) p=p.substr(0,s);
    return p;
}
static std::wstring LogPath(){ return ExeDir() + L"\\vlcmhost_debug.log"; }
static void DbgLog(const std::wstring& tag, const std::wstring& body){
    std::ofstream f(LogPath().c_str(), std::ios::app|std::ios::binary);
    if(!f) return;
    std::string s = "\r\n===== " + HttpUtf8(tag) + " =====\r\n" + HttpUtf8(body) + "\r\n";
    f.write(s.data(), s.size());
}

// =====================  WinHTTP helper  =====================
// Tra ve body (UTF-8 -> wstring). method = L"GET" / L"POST".
static std::wstring HttpRequest(const std::wstring& host, INTERNET_PORT port,
                                bool https, const std::wstring& path,
                                const std::wstring& method,
                                const std::string& body = {},
                                const std::wstring& extraHeaders = L"")
{
    std::wstring out;
    HINTERNET hS = WinHttpOpen(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
        L"AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hS) { DbgLog(L"HTTP LOI", L"WinHttpOpen err=" + std::to_wstring(GetLastError())); return out; }
    // resolve / connect / send / receive (ms) — mac dinh cua WinHTTP la 60s-30s, qua lau
    WinHttpSetTimeouts(hS, 10000, 10000, 15000, 20000);
    HINTERNET hC = WinHttpConnect(hS, host.c_str(), port, 0);
    if (hC) {
        DWORD f = https ? WINHTTP_FLAG_SECURE : 0;
        HINTERNET hR = WinHttpOpenRequest(hC, method.c_str(), path.c_str(),
            NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, f);
        if (hR) {
            std::wstring hdr = L"Accept: application/json,text/plain,text/html,*/*\r\n"
                               L"Accept-Language: vi-VN,vi;q=0.9,en-US;q=0.8,en;q=0.7\r\n";
            if (method == L"POST")
                hdr += L"Content-Type: application/json; charset=utf-8\r\n";
            hdr += extraHeaders;
            BOOL sent = WinHttpSendRequest(hR, hdr.c_str(), (DWORD)-1L,
                (LPVOID)(body.empty()?WINHTTP_NO_REQUEST_DATA:(LPVOID)body.data()),
                (DWORD)body.size(), (DWORD)body.size(), 0);
            if (!sent)
                DbgLog(L"HTTP LOI", method + L" " + path + L" -> WinHttpSendRequest err=" +
                       std::to_wstring(GetLastError()) +
                       L" (12002=timeout, 12007=khong phan giai duoc ten, 12029=khong ket noi duoc, 12175=loi SSL)");
            if (sent && WinHttpReceiveResponse(hR, NULL)) {
                DWORD code = 0, sz = sizeof(code);
                WinHttpQueryHeaders(hR, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                    WINHTTP_HEADER_NAME_BY_INDEX, &code, &sz, WINHTTP_NO_HEADER_INDEX);
                if (code != 200)
                    DbgLog(L"HTTP LOI", method + L" " + path + L" -> HTTP " + std::to_wstring(code));
                std::string raw; DWORD avail = 0;
                do {
                    avail = 0; WinHttpQueryDataAvailable(hR, &avail);
                    if (!avail) break;
                    std::string buf(avail, 0); DWORD rd = 0;
                    WinHttpReadData(hR, &buf[0], avail, &rd);
                    raw.append(buf.data(), rd);
                } while (avail > 0);
                int n = MultiByteToWideChar(CP_UTF8, 0, raw.data(), (int)raw.size(), NULL, 0);
                out.resize(n);
                MultiByteToWideChar(CP_UTF8, 0, raw.data(), (int)raw.size(), &out[0], n);
            }
            WinHttpCloseHandle(hR);
        }
        WinHttpCloseHandle(hC);
    }
    WinHttpCloseHandle(hS);
    return out;
}

// <kenh> trong accounts.xml: "Kenh 3" / "3" -> 3; rong / khong co so -> 0 (nguoi choi tu chon)
static int ParseLine(const std::wstring& s){
    int v = 0; bool any = false;
    for (wchar_t c : s) {
        if (c >= L'0' && c <= L'9') { v = v*10 + (c - L'0'); any = true; }
        else if (any) break;
    }
    return any ? v : 0;
}

static std::wstring HexHr(HRESULT hr){
    wchar_t b[16]; swprintf_s(b, L"0x%08X", (unsigned)hr); return b;
}

// Chi mo link http/https (khong de SWF chay file/lenh tuy y qua ShellExecute)
static void OpenUrl(const std::wstring& url){
    if (url.rfind(L"https://",0)==0 || url.rfind(L"http://",0)==0)
        ShellExecuteW(NULL, L"open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
    else
        DbgLog(L"FlashCall", L"Bo qua URL khong phai http/https: " + url);
}

static bool RegexFirst(const std::wstring& s, const std::wstring& pat, std::wstring& cap) {
    std::wsmatch m; std::wregex re(pat, std::regex::icase);
    if (std::regex_search(s, m, re) && m.size() > 1) { cap = m[1].str(); return true; }
    return false;
}

// =====================  Ket qua login (cong tepaylink)  =====================
struct LoginResult {
    std::wstring token, sessionId;
    // TOAN BO cap key:value trong "var parameters = {...}" cua gamepage.asp,
    // giu dung thu tu. Trang that co ~28 tham so (url, cacheVer, lan_*, config,
    // configData, main, game, baseDir, dataDir, loginServer, loginPort, chatPort,
    // auth, sign, ...) — TGameLoader.swf can gan nhu tat ca, nen truyen het.
    std::vector<std::pair<std::wstring,std::wstring>> params;
    std::wstring movieUrl;              // ket qua cuoi de dua vao Flash
    std::wstring err;                   // ly do that bai (hien cho nguoi dung)
    bool ok = false;
    std::wstring Get(const std::wstring& k) const {
        for (auto& kv : params) if (kv.first == k) return kv.second;
        return L"";
    }
};

static const wchar_t* API_HOST = L"login-vlcm.tpl.vn";   // cong tepaylink

// Escape chuoi cho JSON (pass co dau " hoac \ se lam hong body neu khong escape)
static std::string JsonEsc(const std::string& s) {
    std::string o;
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += (char)c; }
        else if (c < 0x20) { char b[8]; sprintf_s(b, "\\u%04x", c); o += b; }
        else o += (char)c;
    }
    return o;
}

// Giong encodeURIComponent cua JS (swfobject dung ham nay khi dung flashvars).
// BAT BUOC: auth la base64, co the chua '+', '/', '=' — '+' khong encode se
// bi Flash doc thanh dau cach -> server tu choi auth.
static std::wstring UrlEnc(const std::wstring& w) {
    std::string s = HttpUtf8(w), o;
    const char* hex = "0123456789ABCDEF";
    for (unsigned char c : s) {
        if (isalnum(c) || strchr("-_.!~*'()", c)) o += (char)c;
        else { o += '%'; o += hex[c >> 4]; o += hex[c & 15]; }
    }
    return std::wstring(o.begin(), o.end());
}

// Rut toan bo key:value trong "var parameters = { ... };"
// Chap nhan ca gia tri co nhay ("..." / '...') lan KHONG nhay (cacheVer:179,
// loginPort:9000, chatPort:9001).
static bool ParseParameters(const std::wstring& page,
                            std::vector<std::pair<std::wstring,std::wstring>>& out)
{
    size_t p = page.find(L"var parameters");
    if (p == std::wstring::npos) return false;
    size_t a = page.find(L'{', p), b = page.find(L"};", p);
    if (a == std::wstring::npos || b == std::wstring::npos || b < a) return false;
    std::wstring blk = page.substr(a + 1, b - a - 1);
    std::wregex re(L"([A-Za-z_]\\w*)\\s*:\\s*(?:\"([^\"]*)\"|'([^']*)'|([^,\\r\\n}]+))");
    for (std::wsregex_iterator it(blk.begin(), blk.end(), re), e; it != e; ++it) {
        const auto& m = *it;
        std::wstring v = m[2].matched ? m[2].str() : m[3].matched ? m[3].str() : m[4].str();
        while (!v.empty() && iswspace(v.back())) v.pop_back();   // "9000 " -> "9000"
        out.emplace_back(m[1].str(), v);
    }
    return !out.empty();
}

// Luong tepaylink that (xac minh bang Fiddler, 24/09/2026):
//   (1) POST /api/v2/User/GetToken  {"username","password","device":"Windows"} -> "token"
//   (2) POST /api/v2/User/login     {"username","token","device":"Windows"}    -> "sessionId"
//   (3) GET  /api/gamepage/gamepage.asp?ServerID=..&SessionId=..&Device=windows
//            -> var parameters {...}
//   (4) Movie = parameters.game (TGameLoader.swf tren cdn-mct.tepaylink.vn),
//       FlashVars = TOAN BO parameters (encodeURIComponent tung gia tri).
static LoginResult DoLogin(const std::wstring& user, const std::wstring& pass,
                           const std::wstring& serverId /*vd "1"*/)
{
    LoginResult r;
    const INTERNET_PORT PORT = INTERNET_DEFAULT_HTTPS_PORT;
    std::string u = JsonEsc(HttpUtf8(user)), p = JsonEsc(HttpUtf8(pass));
    std::wstring status;

    // --- 1) GetToken: username + password -> token ---
    std::string body1 = "{\"username\":\"" + u + "\",\"password\":\"" + p +
                        "\",\"device\":\"Windows\"}";
    std::wstring resp1 = HttpRequest(API_HOST, PORT, true,
                                     L"/api/v2/User/GetToken", L"POST", body1);
    DbgLog(L"GetToken resp", resp1.empty()? L"(RONG - khong nhan duoc gi)" : resp1);
    RegexFirst(resp1, L"\"status\"\\s*:\\s*(-?\\d+)", status);
    if (status != L"1" || !RegexFirst(resp1, L"\"token\"\\s*:\\s*\"([^\"]+)\"", r.token)) {
        RegexFirst(resp1, L"\"content\"\\s*:\\s*\"([^\"]*)\"", r.err);   // vd "Sai ID hoac pass"
        if (resp1.empty()) r.err = L"Khong ket noi duoc login-vlcm.tpl.vn (xem log HTTP LOI)";
        r.err = L"Buoc GetToken: " + (r.err.empty() ? L"status=" + status : r.err);
        DbgLog(L"KET QUA", r.err);
        return r;
    }

    // --- 2) login: username + token -> sessionId ---
    std::string body2 = "{\"username\":\"" + u + "\",\"token\":\"" + JsonEsc(HttpUtf8(r.token)) +
                        "\",\"device\":\"Windows\"}";
    std::wstring resp2 = HttpRequest(API_HOST, PORT, true,
                                     L"/api/v2/User/login", L"POST", body2);
    DbgLog(L"login resp", resp2.empty()? L"(RONG - khong nhan duoc gi)" : resp2);
    status.clear();
    RegexFirst(resp2, L"\"status\"\\s*:\\s*(-?\\d+)", status);
    if (status != L"1" || !RegexFirst(resp2, L"\"sessionId\"\\s*:\\s*\"([^\"]+)\"", r.sessionId)) {
        RegexFirst(resp2, L"\"content\"\\s*:\\s*\"([^\"]*)\"", r.err);
        r.err = L"Buoc login: " + (r.err.empty() ? L"status=" + status : r.err);
        DbgLog(L"KET QUA", r.err);
        return r;
    }

    // --- 3) gamepage.asp -> var parameters ---
    std::wstring page = HttpRequest(API_HOST, PORT, true,
        L"/api/gamepage/gamepage.asp?ServerID=" + serverId +
        L"&SessionId=" + r.sessionId + L"&Device=windows", L"GET");
    DbgLog(L"gamepage resp", page.empty()? L"(RONG)" : page);
    if (!ParseParameters(page, r.params) || r.Get(L"auth").empty() || r.Get(L"sign").empty()) {
        r.err = page.empty() ? L"Buoc gamepage: khong nhan duoc trang"
                             : L"Buoc gamepage: khong co var parameters / auth / sign "
                               L"(SessionId het han hoac ServerID sai?)";
        DbgLog(L"KET QUA", r.err);
        return r;
    }
    // TGameLoader.loadMain(): khong co "config" (va khong co "data") -> vong lap
    // while(true){} -> Flash TREO CUNG khong bao loi. "main" = TGame.tse.
    if (r.Get(L"config").empty() || r.Get(L"main").empty() || r.Get(L"game").empty()) {
        r.err = L"gamepage thieu config/main/game -> Flash se treo, dung lai";
        DbgLog(L"KET QUA", r.err);
        return r;
    }

    // --- 4) Movie = game + TOAN BO parameters (tru "game") noi vao query string ---
    // Y het vlcmcliet.exe (xac minh bang Fiddler): GET .../TGameLoader.swf?ver=..&url=..&
    // cacheVer=..&...&auth=..&sign=..  — gia tri ma hoa kieu encodeURIComponent.
    // Flash doc query string cua SWF qua loaderInfo.parameters (giong FlashVars).
    r.movieUrl = r.Get(L"game");
    for (auto& kv : r.params) {
        if (kv.first == L"game") continue;
        r.movieUrl += (r.movieUrl.find(L'?') == std::wstring::npos ? L"?" : L"&");
        r.movieUrl += kv.first + L"=" + UrlEnc(kv.second);
    }
    DbgLog(L"KET QUA", L"OK  game=" + r.Get(L"game") +
                       L"\r\nloginServer=" + r.Get(L"loginServer") + L":" + r.Get(L"loginPort") +
                       L"\r\nchatServer="  + r.Get(L"chatServer")  + L":" + r.Get(L"chatPort") +
                       L"\r\nso tham so=" + std::to_wstring(r.params.size()) +
                       L"  do dai URL=" + std::to_wstring(r.movieUrl.size()));
    r.ok = true;
    return r;
}

// Lenh tu pipe (luong pipe) -> luong UI. LPARAM = new std::wstring (luong UI delete).
static const UINT WM_VLCM_PIPE = WM_APP + 1;

// =====================  Cua so chinh + host Flash (ATL)  =====================
class MainFrame : public CWindowImpl<MainFrame> {
public:
    CAxWindow           m_ax;      // vung chua ActiveX
    CComPtr<IDispatch>  m_flash;   // control Flash (IShockwaveFlash qua IDispatch)
    FlashBridge         m_bridge;  // sink su kien Flash + cau noi voi VlcmLoader.swf
    std::wstring        m_tips;    // setTips cua game (hien khi dong cua so)
    std::wstring        m_recharge;// tham so "recharge" cua gamepage (cho showPopup)
    bool                m_loaded = false;   // da nap game it nhat mot lan
    PipeServer          m_pipe;    // \\.\pipe\vlcmhost-<tai khoan> cho vlcmctl.exe

    DECLARE_WND_CLASS(L"VLCMFlashHost")
    BEGIN_MSG_MAP(MainFrame)
        MESSAGE_HANDLER(WM_CREATE,  OnCreate)
        MESSAGE_HANDLER(WM_SIZE,    OnSize)
        MESSAGE_HANDLER(WM_CLOSE,   OnClose)
        MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
        MESSAGE_HANDLER(WM_VLCM_PIPE, OnPipeLine)
    END_MSG_MAP()

    LRESULT OnCreate(UINT,WPARAM,LPARAM,BOOL&) {
        // 1. TRUOC khi tao control: thu muc exe chay o sandbox localTrusted, de VlcmLoader.swf
        //    (file local) tai duoc TGameLoader va doc duoc lop cua game.
        if (!FlashBridge::WriteTrustFile(ExeDir()))
            DbgLog(L"TRUST", L"Khong ghi duoc file FlashPlayerTrust cho " + ExeDir());
        // Handler dat mot lan, dung lai moi khi tao lai control.
        m_bridge.SetEventHandler([this](const std::wstring& n, const std::wstring& d) { OnLoaderEvent(n, d); });
        m_bridge.SetCallHandler([this](const FlashBridge::InvokeCall& c) { return OnGameCall(c); });
        CreateFlash();
        return 0;
    }
    LRESULT OnSize(UINT,WPARAM,LPARAM lp,BOOL&) {
        if (m_ax.m_hWnd) m_ax.SetWindowPos(NULL,0,0,LOWORD(lp),HIWORD(lp),SWP_NOZORDER);
        return 0;
    }
    // Trang web hoi "Disconnect the game" khi dong; game con gui loi nhac qua setTips
    LRESULT OnClose(UINT,WPARAM,LPARAM,BOOL&){
        if (!m_tips.empty() &&
            ::MessageBoxW(m_hWnd, (m_tips + L"\nVan thoat game?").c_str(), L"vlcmHost",
                          MB_YESNO | MB_ICONQUESTION) != IDYES)
            return 0;                       // nguoi dung chon "No" -> khong dong
        DestroyWindow();
        return 0;
    }
    LRESULT OnDestroy(UINT,WPARAM,LPARAM,BOOL&){
        m_pipe.Send(L"! hostclose");        // bao vlcmpanel: dong theo y nguoi dung, dung tu mo lai
        Sleep(150);
        m_pipe.Stop();                      // dung pipe truoc: khong con lenh nao goi vao Flash
        DestroyFlash();                     // Detach truoc khi huy control
        PostQuitMessage(0); return 0;
    }

    // --- tao / huy control Flash ---
    bool CreateFlash() {
        RECT rc; GetClientRect(&rc);
        // Tao control Flash bang PROGID - reg-free COM lo phan phan giai CLSID
        m_ax.Create(m_hWnd, rc, L"ShockwaveFlash.ShockwaveFlash",
                    WS_CHILD | WS_VISIBLE, 0, (UINT)1001);
        m_ax.QueryControl(IID_IDispatch, (void**)&m_flash);
        if (!m_flash) {
            DbgLog(L"FLASH LOI", L"Khong tao duoc control Flash (thieu data\\Flash.ocx / manifest?)");
            return false;
        }
        HRESULT hr = m_bridge.Attach(m_flash);
        if (FAILED(hr)) DbgLog(L"FLASH LOI", L"FlashBridge::Attach that bai " + HexHr(hr));
        SetFlashProp(L"AllowScriptAccess", L"always"); // cho phep ExternalInterface
        return true;
    }
    void DestroyFlash() {
        m_bridge.Detach();
        m_flash.Release();
        if (m_ax.m_hWnd) m_ax.DestroyWindow();
    }

    // --- goi property/method Flash qua IDispatch ---
    HRESULT SetFlashProp(LPCOLESTR name, LPCOLESTR val) {
        if (!m_flash) return E_FAIL;
        DISPID id; LPOLESTR n=(LPOLESTR)name;
        if (FAILED(m_flash->GetIDsOfNames(IID_NULL,&n,1,LOCALE_USER_DEFAULT,&id)))
            return E_FAIL;
        CComVariant v(val);
        DISPPARAMS dp={&v,0,1,0}; DISPID put=DISPID_PROPERTYPUT;
        dp.rgdispidNamedArgs=&put; dp.cNamedArgs=1;
        return m_flash->Invoke(id,IID_NULL,LOCALE_USER_DEFAULT,
            DISPATCH_PROPERTYPUT,&dp,NULL,NULL,NULL);
    }
    HRESULT CallFlash1(LPCOLESTR method, LPCOLESTR arg) {
        if (!m_flash) return E_FAIL;
        DISPID id; LPOLESTR n=(LPOLESTR)method;
        if (FAILED(m_flash->GetIDsOfNames(IID_NULL,&n,1,LOCALE_USER_DEFAULT,&id)))
            return E_FAIL;
        CComVariant v(arg); DISPPARAMS dp={&v,0,1,0};
        return m_flash->Invoke(id,IID_NULL,LOCALE_USER_DEFAULT,
            DISPATCH_METHOD,&dp,NULL,NULL,NULL);
    }

    // ====== "BOM LINK GAME LEN FLASH" ======
    // Movie = VlcmLoader.swf canh exe; link TGameLoader (lr.movieUrl, da ma hoa %2B %2F %3D)
    // di qua cfg.url. VlcmLoader goi vlcm_get_config() de lay link roi tu nap game.
    void LoadGame(const LoginResult& lr) {
        // Nap lai (F5): tao lai control. Dong sach socket/bo nho cua game cu, va chac chan
        // Flash nap lai du Movie trung duong dan voi lan truoc.
        if (m_loaded) {
            DestroyFlash();
            if (!CreateFlash()) return;
        }
        m_loaded = true;

        FlashBridge::Config cfg;
        cfg.url      = lr.movieUrl;
        cfg.line     = m_line;     // 0 = nguoi choi tu chon kenh
        cfg.charName = m_char;     // L"" = nguoi choi tu chon nhan vat
        m_bridge.SetConfig(cfg);

        SetFlashProp(L"FlashVars",         L"");       // tham so nam trong URL, khong dung FlashVars
        SetFlashProp(L"AllowNetworking",   L"all");    // can cho Socket 9000/9001/9002
        SetFlashProp(L"AllowScriptAccess", L"always");

        std::wstring swf = ExeDir() + L"\\VlcmLoader.swf";
        if (GetFileAttributesW(swf.c_str()) != INVALID_FILE_ATTRIBUTES) {
            DbgLog(L"LOADER", L"Movie=" + swf + L"  kenh=" + std::to_wstring(m_line) + L"  nv=" + m_char);
            SetFlashProp(L"Movie", swf.c_str());
        } else {
            DbgLog(L"LOADER", L"Khong thay " + swf + L" -> nap thang TGameLoader, chon kenh/nhan vat bang tay");
            SetFlashProp(L"Movie", lr.movieUrl.c_str());
        }
        // KHONG goi LoadMovie: ham nay can 2 tham so (layer, url).
    }

    // ====== Su kien tu VlcmLoader.swf ======
    // Chay BEN TRONG FlashCall: chi cap nhat giao dien / log, khong huy control o day.
    void OnLoaderEvent(const std::wstring& name, const std::wstring& detail) {
        OutputDebugStringW((L"[vlcm] " + name + L": " + detail + L"\n").c_str());
        m_pipe.Send(L"! " + name + L" " + OneLine(detail));      // cho vlcmctl.exe (neu dang ket noi)
        if (name == L"train") {
            if (detail.rfind(L"state ", 0) == 0 || detail.rfind(L"error ", 0) == 0 ||
                detail.rfind(L"start ", 0) == 0 || detail.rfind(L"stop ", 0) == 0 || detail.rfind(L"revive ", 0) == 0)
                DbgLog(L"TRAIN", detail);                          // "status ..." 5 giay/lan: khong ghi file
            if (detail.rfind(L"state ", 0) == 0) SetStatus(L"train: " + detail.substr(6));
            return;
        }
        if (name == L"status") {
            if (detail.rfind(L"state=", 0) == 0)         SetStatus(detail.substr(6));
            else if (detail.rfind(L"sandbox=", 0) == 0)  DbgLog(L"LOADER status", detail);
            return;                                       // cac status khac chi ra DebugView
        }
        DbgLog(L"LOADER " + name, detail);
        if      (name == L"entered_game")   SetStatus(L"trong game");
        else if (name == L"disconnected")   SetStatus(L"mat ket noi - cho game tu vao lai, hoac F5");
        else if (name == L"line_not_found") SetStatus(L"khong co kenh " + std::to_wstring(m_line) + L" - chon tay");
        else if (name == L"char_not_found") SetStatus(L"khong thay nhan vat " + m_char + L" - chon tay");
        else if (name == L"error")          SetStatus(L"loader loi, chon tay (chi tiet: log)");
    }

    // ====== Kenh dieu khien (vlcmctl.exe) ======
    static std::wstring OneLine(std::wstring s) {
        for (auto& c : s) if (c == L'\n' || c == L'\r') c = L' ';
        return s;
    }
    void StartPipe() {
        std::wstring acc = m_user;
        for (auto& c : acc) if (!iswalnum(c) && c != L'_' && c != L'-' && c != L'.') c = L'_';
        HWND hwnd = m_hWnd;
        m_pipe.Start(L"\\\\.\\pipe\\vlcmhost-" + acc, [hwnd](const std::wstring& line) {
            auto* p = new std::wstring(line);
            if (!::PostMessageW(hwnd, WM_VLCM_PIPE, 0, (LPARAM)p)) delete p;
        });
        DbgLog(L"PIPE", L"Kenh dieu khien: " + m_pipe.Name());
    }
    // Chay tren luong UI (ngoai FlashCall) nen goi CallFunction vao Flash an toan.
    LRESULT OnPipeLine(UINT,WPARAM,LPARAM lp,BOOL&) {
        std::unique_ptr<std::wstring> line((std::wstring*)lp);
        std::wstring cmd = *line, args;
        size_t sp = cmd.find(L' ');
        if (sp != std::wstring::npos) { args = cmd.substr(sp + 1); cmd = cmd.substr(0, sp); }

        if (cmd == L"ping")   { m_pipe.Send(L"> ok pong " + m_user); return 0; }
        if (cmd == L"reload") { m_pipe.Send(L"> ok reload"); LoginAndLoad(); return 0; }
        // vlcmpanel "Tat cua so": thoat gon, khong hoi "Van thoat game?" (cua so an nam ngoai man hinh, hop hoi se bi treo o do)
        if (cmd == L"quit")   { DbgLog(L"PIPE", L"quit tu panel"); m_pipe.Send(L"> ok quit"); Sleep(100); DestroyWindow(); return 0; }
        if (!m_flash)         { m_pipe.Send(L"> err Flash chua tao"); return 0; }

        std::wstring res;
        HRESULT hr = m_bridge.CallString(L"vlcm_command", { cmd, args }, &res);
        if (FAILED(hr)) res = L"err Flash chua san sang (VlcmLoader chua nap?) " + HexHr(hr);
        else if (res.empty()) res = L"err khong co tra loi";
        m_pipe.Send(L"> " + OneLine(res));
        return 0;
    }

    // ====== Loi goi ExternalInterface cua GAME (truoc day o FlashCallSink.h) ======
    //   console.log(msg)                      -> DebugView
    //   window.open(url, "_target", "")       -> mo trinh duyet (chi http/https)
    //   showPopup(pingtai, serverID, name)    -> mo trang nap tien (tham so "recharge")
    //   setTips(text)                         -> luu, hien khi dong cua so
    std::wstring OnGameCall(const FlashBridge::InvokeCall& c) {
        auto arg = [&](size_t i) { return i < c.args.size() ? c.args[i] : std::wstring(); };
        if (c.name == L"console.log") {
            OutputDebugStringW((L"[TGame] " + arg(0) + L"\n").c_str());
        } else if (c.name == L"window.open") {
            DbgLog(L"FlashCall", L"window.open: " + arg(0));
            OpenUrl(arg(0));
        } else if (c.name == L"showPopup") {
            DbgLog(L"FlashCall", L"showPopup: pingtai=" + arg(0) + L" serverID=" + arg(1) + L" name=" + arg(2));
            if (!m_recharge.empty()) OpenUrl(m_recharge);
        } else if (c.name == L"setTips") {
            m_tips = arg(0);
        } else {
            DbgLog(L"FlashCall", L"Ham chua xu ly: " + c.name);
        }
        return L"<null/>";   // game khong dung gia tri tra ve
    }

    // ====== Dang nhap (lai) - dung cho lan dau va phim F5 ======
    // auth co truong time -> het han; khi mat ket noi phai lam lai CA 3 buoc HTTP,
    // giong nut "Tai lai" tren trang web (reload trang = goi lai gamepage.asp).
    std::wstring m_user, m_pass, m_sv;
    int          m_line = 0;       // lineID can vao (tu <kenh>), 0 = chon tay
    std::wstring m_char;           // ten nhan vat (tu <nv>), rong = chon tay
    bool m_busy = false;

    void SetStatus(const std::wstring& s) {
        ::SetWindowTextW(m_hWnd, (L"VLCM Host - " + m_user + L" - S" + (m_sv.empty() ? L"1" : m_sv) +
                                  L" - " + s + L"  (F5 = tai lai)").c_str());
    }

    void LoginAndLoad() {
        if (m_busy || m_user.empty()) return;
        m_busy = true;
        SetStatus(L"dang dang nhap...");
        LoginResult lr = DoLogin(m_user, m_pass, m_sv.empty() ? L"1" : m_sv);
        if (lr.ok) {
            m_recharge = lr.Get(L"recharge");   // cho showPopup (nap tien)
            m_tips.clear();
            SetStatus(L"dang tai game");        // truoc LoadGame: su kien cua loader se ghi de len
            LoadGame(lr);
        } else {
            SetStatus(L"LOI");
            ::MessageBoxW(m_hWnd, (L"Dang nhap that bai:\n" + lr.err +
                         L"\n\nChi tiet: vlcmhost_debug.log").c_str(), L"vlcmHost", MB_OK | MB_ICONWARNING);
        }
        m_busy = false;
    }
};

int WINAPI wWinMain(HINSTANCE hInst,HINSTANCE,LPWSTR,int nShow) {
    OleInitialize(NULL);                 // BAT BUOC de host ActiveX
    CComModule mod; mod.Init(NULL,hInst);
    AtlAxWinInit();                      // dang ky class "AtlAxWin"

    MainFrame f;
    RECT rc={100,100,1180,820};
    f.Create(NULL,rc,L"VLCM Host",WS_OVERLAPPEDWINDOW);
    f.ShowWindow(nShow); f.UpdateWindow();

    // --- Doc accounts.xml, login bang acc dau tien chua an (hidden!=1) ---
    std::vector<Account> accs = LoadAccounts(AccountsXmlPath());
    DbgLog(L"START", L"accounts doc duoc: " + std::to_wstring(accs.size()) +
                     L" | file: " + AccountsXmlPath());
    // vlcmhost.exe --acc <tai khoan>  (vlcmpanel mo moi cua so cho mot tai khoan); khong co thi lay acc dau tien chua an
    std::wstring wantAcc;
    {
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        for (int i = 1; argv && i + 1 < argc; ++i) if (std::wstring(argv[i]) == L"--acc") wantAcc = argv[i + 1];
        if (argv) LocalFree(argv);
    }
    const Account* pick = nullptr;
    if (!wantAcc.empty()) {
        for (const auto& a : accs) if (_wcsicmp(a.user.c_str(), wantAcc.c_str()) == 0) { pick = &a; break; }
        if (!pick) {
            MessageBoxW(f.m_hWnd, (L"Khong thay tai khoan " + wantAcc + L" trong data\\accounts.xml").c_str(), L"vlcmHost", MB_OK);
            return 0;
        }
    } else {
        for (const auto& a : accs) if (a.hidden != L"1") { pick = &a; break; }
        if (!pick && !accs.empty()) pick = &accs[0];
    }

    if (pick) {
        DbgLog(L"PICK", L"cong=" + pick->cong + L" user=" + pick->user +
                        L" sv=" + pick->sv + L" kenh=" + pick->kenh + L" nv=" + pick->nv +
                        L" pass_len=" +
                        std::to_wstring(pick->pass.size()));
        f.m_user = pick->user; f.m_pass = pick->pass; f.m_sv = pick->sv;
        f.m_line = ParseLine(pick->kenh);   // "Kenh 1" -> 1; rong -> 0 (chon tay)
        f.m_char = pick->nv;                // rong -> chon tay
        f.StartPipe();                      // truoc LoginAndLoad (HTTP chan luong UI vai giay)
        f.LoginAndLoad();
    } else {
        MessageBoxW(f.m_hWnd, L"Khong doc duoc data\\accounts.xml", L"vlcmHost", MB_OK);
    }

    MSG m;
    while (GetMessage(&m,NULL,0,0)) {
        // F5: dang nhap lai tu dau + nap lai game (Flash giu focus ban phim nen
        // phai bat o vong lap, khong nhan duoc WM_KEYDOWN o MainFrame).
        if (m.message == WM_KEYDOWN && m.wParam == VK_F5 &&
            (m.hwnd == f.m_hWnd || ::IsChild(f.m_hWnd, m.hwnd))) {
            f.LoginAndLoad();
            continue;
        }
        TranslateMessage(&m); DispatchMessage(&m);
    }

    mod.Term(); OleUninitialize();
    return 0;
}
