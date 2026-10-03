#include "PipeServer.h"

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

bool PipeServer::Start(const std::wstring& pipeName, LineHandler onLine) {
    Stop();
    name_ = pipeName;
    onLine_ = std::move(onLine);
    stopEv_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    sendEv_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!stopEv_ || !sendEv_) { Stop(); return false; }
    thread_ = std::thread(&PipeServer::Run, this);
    return true;
}

void PipeServer::Stop() {
    if (stopEv_) SetEvent(stopEv_);
    if (thread_.joinable()) thread_.join();
    if (stopEv_) { CloseHandle(stopEv_); stopEv_ = nullptr; }
    if (sendEv_) { CloseHandle(sendEv_); sendEv_ = nullptr; }
    client_ = false;
    std::lock_guard<std::mutex> lk(mu_);
    queue_.clear();
}

void PipeServer::Send(const std::wstring& line) {
    if (!client_ || !sendEv_) return;
    std::wstring one = line;
    for (auto& c : one) if (c == L'\n' || c == L'\r') c = L' ';    // luôn đúng một dòng
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (queue_.size() >= 1000) queue_.pop_front();              // tool đọc chậm: bỏ bớt sự kiện cũ
        queue_.push_back(ToUtf8(one) + "\n");
    }
    SetEvent(sendEv_);
}

void PipeServer::Run() {
    HANDLE connEv = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    while (WaitForSingleObject(stopEv_, 0) != WAIT_OBJECT_0) {
        HANDLE pipe = CreateNamedPipeW(name_.c_str(),
            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
            1, 64 * 1024, 64 * 1024, 0, nullptr);
        if (pipe == INVALID_HANDLE_VALUE) {
            // Tên đang bị một vlcmhost khác (cùng tài khoản) giữ: thử lại sau.
            if (WaitForSingleObject(stopEv_, 2000) == WAIT_OBJECT_0) break;
            continue;
        }

        OVERLAPPED ov = {};
        ov.hEvent = connEv;
        ResetEvent(connEv);
        bool connected = false;
        if (ConnectNamedPipe(pipe, &ov)) {
            connected = true;
        } else {
            DWORD err = GetLastError();
            if (err == ERROR_PIPE_CONNECTED) connected = true;
            else if (err == ERROR_IO_PENDING) {
                HANDLE hs[2] = { stopEv_, connEv };
                if (WaitForMultipleObjects(2, hs, FALSE, INFINITE) == WAIT_OBJECT_0 + 1) {
                    DWORD n = 0;
                    connected = GetOverlappedResult(pipe, &ov, &n, FALSE) != FALSE;
                } else {
                    CancelIo(pipe);
                }
            }
        }

        if (connected) {
            { std::lock_guard<std::mutex> lk(mu_); queue_.clear(); }   // không gửi sự kiện cũ cho tool mới
            client_ = true;
            ServeClient(pipe);
            client_ = false;
            DisconnectNamedPipe(pipe);
        }
        CloseHandle(pipe);
    }
    CloseHandle(connEv);
}

bool PipeServer::WriteAll(HANDLE pipe, const std::string& data, HANDLE ev) {
    OVERLAPPED ov = {};
    ov.hEvent = ev;
    ResetEvent(ev);
    DWORD n = 0;
    if (!WriteFile(pipe, data.data(), (DWORD)data.size(), nullptr, &ov)) {
        if (GetLastError() != ERROR_IO_PENDING) return false;
        if (WaitForSingleObject(ev, 5000) != WAIT_OBJECT_0) { CancelIo(pipe); return false; }
    }
    return GetOverlappedResult(pipe, &ov, &n, FALSE) && n == data.size();
}

bool PipeServer::ServeClient(HANDLE pipe) {
    HANDLE rdEv = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE wrEv = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    char buf[4096];
    std::string pending;
    OVERLAPPED rov = {};
    bool alive = true;
    bool reading = false;

    while (alive) {
        if (!reading) {
            ZeroMemory(&rov, sizeof(rov));
            rov.hEvent = rdEv;
            ResetEvent(rdEv);
            if (!ReadFile(pipe, buf, sizeof(buf), nullptr, &rov) && GetLastError() != ERROR_IO_PENDING) break;
            reading = true;       // xong ngay hay chưa thì event đều được bật; xử lý chung ở dưới
        }

        HANDLE hs[3] = { stopEv_, rdEv, sendEv_ };
        DWORD w = WaitForMultipleObjects(3, hs, FALSE, INFINITE);
        if (w == WAIT_OBJECT_0) break;

        if (w == WAIT_OBJECT_0 + 1) {
            DWORD n = 0;
            reading = false;
            if (!GetOverlappedResult(pipe, &rov, &n, FALSE) || n == 0) break;     // tool đã ngắt
            pending.append(buf, n);
            if (pending.size() > 64 * 1024) pending.clear();                     // dòng quá dài: bỏ
            size_t pos;
            while ((pos = pending.find('\n')) != std::string::npos) {
                std::string one = pending.substr(0, pos);
                pending.erase(0, pos + 1);
                if (!one.empty() && one.back() == '\r') one.pop_back();
                if (!one.empty() && onLine_) onLine_(FromUtf8(one));
            }
        }

        // Luôn xả hàng đợi (sendEv_ là auto-reset nên có thể đã bị lần chờ trước tiêu mất).
        for (;;) {
            std::string out;
            {
                std::lock_guard<std::mutex> lk(mu_);
                if (queue_.empty()) break;
                out.swap(queue_.front());
                queue_.pop_front();
            }
            if (!WriteAll(pipe, out, wrEv)) { alive = false; break; }
        }
    }

    if (reading) {
        CancelIo(pipe);
        DWORD n = 0;
        GetOverlappedResult(pipe, &rov, &n, TRUE);
    }
    CloseHandle(rdEv);
    CloseHandle(wrEv);
    return true;
}
