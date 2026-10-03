// vlcmctl.cpp - tool dieu khien vlcmhost tu ben ngoai qua named pipe \\.\pipe\vlcmhost-<tai khoan>
//
//   vlcmctl list                          liet ke cac cua so vlcmhost dang chay
//   vlcmctl <tk> <lenh> [tham so...]      gui mot lenh, in ket qua roi thoat
//   vlcmctl all <lenh> [tham so...]       gui cho moi cua so
//   vlcmctl <tk>                          che do tuong tac: go lenh, xem su kien truc tiep (Ctrl+C de thoat)
//
// Luu y: moi vlcmhost chi nhan MOT tool noi vao cung luc. Dang mo vlcmpanel thi vlcmctl se bao
// "dang ban" (va nguoc lai) - dung mot trong hai thoi.
//
// Lenh (xem VlcmTrain.as):
//   where | status | train_start [map= x= y=] [r=8] [keys=1,2,3] [keycd=3000] [hpkey=9] [hp=40] [types=1]
//   train_stop | ping | reload
//
// Build (MSVC):  cl /EHsc /std:c++17 /utf-8 vlcmctl.cpp
// Build (MinGW): g++ -std=c++17 -municode -O2 -static vlcmctl.cpp -o vlcmctl.exe

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <atomic>
#include <thread>
#include <vector>

static std::atomic<bool> g_quitting{false};

static const wchar_t* PIPE_PREFIX = L"\\\\.\\pipe\\";
static const wchar_t* NAME_PREFIX = L"vlcmhost-";

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

// In ra console dung tieng Viet (WriteConsoleW); neu bi chuyen huong ra file thi ghi UTF-8.
static void Print(const std::wstring& s) {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode, n;
    std::wstring line = s + L"\n";
    if (GetConsoleMode(h, &mode)) WriteConsoleW(h, line.c_str(), (DWORD)line.size(), &n, nullptr);
    else { std::string u = ToUtf8(line); WriteFile(h, u.data(), (DWORD)u.size(), &n, nullptr); }
}

static std::vector<std::wstring> ListHosts() {
    std::vector<std::wstring> out;
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW((std::wstring(PIPE_PREFIX) + L"*").c_str(), &fd);
    if (f == INVALID_HANDLE_VALUE) return out;
    do {
        std::wstring n = fd.cFileName;
        if (n.compare(0, wcslen(NAME_PREFIX), NAME_PREFIX) == 0) out.push_back(n.substr(wcslen(NAME_PREFIX)));
    } while (FindNextFileW(f, &fd));
    FindClose(f);
    return out;
}

class Conn {
public:
    ~Conn() { if (h_ != INVALID_HANDLE_VALUE) CloseHandle(h_); if (ev_) CloseHandle(ev_); if (wev_) CloseHandle(wev_); }

    bool busy_ = false;
    std::wstring FailText() const {
        return busy_ ? L"dang ban - vlcmpanel (hoac vlcmctl khac) dang ket noi, tat no roi thu lai"
                     : L"khong ket noi duoc (vlcmhost chua chay?)";
    }
    bool Open(const std::wstring& account) {
        std::wstring name = std::wstring(PIPE_PREFIX) + NAME_PREFIX + account;
        for (int i = 0; i < 20; ++i) {
            h_ = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
            if (h_ != INVALID_HANDLE_VALUE) break;
            DWORD e = GetLastError();
            if (e == ERROR_PIPE_BUSY) { busy_ = true; WaitNamedPipeW(name.c_str(), 500); continue; }   // co tool khac dang ket noi
            if (e == ERROR_FILE_NOT_FOUND && i < 3) { Sleep(300); continue; }
            return false;
        }
        if (h_ == INVALID_HANDLE_VALUE) return false;
        ev_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        wev_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        return true;
    }

    bool Write(const std::wstring& line) {
        std::string u = ToUtf8(line) + "\n";
        OVERLAPPED ov = {};
        ov.hEvent = wev_;
        ResetEvent(wev_);
        DWORD n = 0;
        if (!WriteFile(h_, u.data(), (DWORD)u.size(), nullptr, &ov)) {
            if (GetLastError() != ERROR_IO_PENDING) return false;
            if (WaitForSingleObject(wev_, 5000) != WAIT_OBJECT_0) { CancelIo(h_); return false; }
        }
        return GetOverlappedResult(h_, &ov, &n, FALSE) && n == u.size();
    }

    // Doc mot dong (khong gom '\n'). timeoutMs < 0: cho mai. Tra false khi mat ket noi hoac het gio.
    bool ReadLine(std::wstring* line, int timeoutMs) {
        for (;;) {
            size_t p = buf_.find('\n');
            if (p != std::string::npos) {
                std::string one = buf_.substr(0, p);
                buf_.erase(0, p + 1);
                if (!one.empty() && one.back() == '\r') one.pop_back();
                *line = FromUtf8(one);
                return true;
            }
            char tmp[4096];
            OVERLAPPED ov = {};
            ov.hEvent = ev_;
            ResetEvent(ev_);
            DWORD n = 0;
            if (!ReadFile(h_, tmp, sizeof(tmp), nullptr, &ov)) {
                if (GetLastError() != ERROR_IO_PENDING) return false;
                if (WaitForSingleObject(ev_, timeoutMs < 0 ? INFINITE : (DWORD)timeoutMs) != WAIT_OBJECT_0) {
                    CancelIo(h_);
                    GetOverlappedResult(h_, &ov, &n, TRUE);
                    return false;
                }
            }
            if (!GetOverlappedResult(h_, &ov, &n, FALSE) || n == 0) return false;
            buf_.append(tmp, n);
        }
    }

private:
    HANDLE h_ = INVALID_HANDLE_VALUE;
    HANDLE ev_ = nullptr, wev_ = nullptr;
    std::string buf_;
};

// Gui mot lenh, cho dong "> ..." (bo qua su kien "! ..."). Tra ma thoat: 0 ok, 1 host tra err, 2 loi ket noi.
static int OneShot(const std::wstring& account, const std::wstring& cmd, bool showEvents) {
    Conn c;
    if (!c.Open(account)) { Print(L"[" + account + L"] " + c.FailText()); return 2; }
    if (!c.Write(cmd)) { Print(L"[" + account + L"] gui lenh that bai"); return 2; }
    std::wstring line;
    while (c.ReadLine(&line, 15000)) {
        if (line.compare(0, 2, L"> ") == 0) {
            Print(L"[" + account + L"] " + line.substr(2));
            return line.compare(2, 3, L"err") == 0 ? 1 : 0;
        }
        if (showEvents) Print(L"[" + account + L"] " + line);
    }
    Print(L"[" + account + L"] khong nhan duoc tra loi");
    return 2;
}

static int Interactive(const std::wstring& account) {
    Conn c;
    if (!c.Open(account)) { Print(L"vlcmhost-" + account + L": " + c.FailText()); return 2; }
    Print(L"Da ket noi vlcmhost-" + account + L". Go lenh (where, status, train_start ..., train_stop), Ctrl+C de thoat.");
    std::thread reader([&c] {
        std::wstring line;
        while (c.ReadLine(&line, -1)) Print(line);
        if (g_quitting) return;
        Print(L"(mat ket noi)");
        ExitProcess(0);
    });
    reader.detach();

    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode;
    bool console = GetConsoleMode(in, &mode) != FALSE;
    for (;;) {
        std::wstring line;
        if (console) {
            wchar_t buf[1024];
            DWORD n = 0;
            if (!ReadConsoleW(in, buf, 1023, &n, nullptr) || n == 0) break;
            line.assign(buf, n);
        } else {
            char buf[1024];
            DWORD n = 0;
            if (!ReadFile(in, buf, sizeof(buf), &n, nullptr) || n == 0) break;
            line = FromUtf8(std::string(buf, n));
        }
        while (!line.empty() && (line.back() == L'\n' || line.back() == L'\r')) line.pop_back();
        if (line.empty()) continue;
        if (line == L"exit" || line == L"quit") break;
        if (!c.Write(line)) { Print(L"(gui that bai)"); break; }
    }
    g_quitting = true;
    ExitProcess(0);     // thoat ngay, khong cho luong doc
    return 0;
}

int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    if (argc < 2 || std::wstring(argv[1]) == L"-h" || std::wstring(argv[1]) == L"help") {
        Print(L"vlcmctl list | vlcmctl <tk|all> <lenh> [tham so...] | vlcmctl <tk>");
        Print(L"Lenh: where, status, train_start [map= x= y=] [r=] [keys=] [keycd=] [hpkey=] [hp=] [types=], train_stop, ping, reload");
        return 0;
    }
    std::wstring target = argv[1];
    if (target == L"list") {
        auto hosts = ListHosts();
        if (hosts.empty()) Print(L"(khong co vlcmhost nao dang chay)");
        for (auto& h : hosts) Print(h);
        return 0;
    }
    if (argc == 2) return Interactive(target);

    std::wstring cmd;
    for (int i = 2; i < argc; ++i) { if (i > 2) cmd += L" "; cmd += argv[i]; }
    if (target == L"all") {
        int rc = 0;
        for (auto& h : ListHosts()) rc = (std::max)(rc, OneShot(h, cmd, false));
        return rc;
    }
    return OneShot(target, cmd, false);
}
