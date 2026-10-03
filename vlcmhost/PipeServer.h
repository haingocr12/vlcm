// PipeServer: kênh điều khiển cục bộ cho vlcmhost qua named pipe (vd \\.\pipe\vlcmhost-haincr23).
//
// Giao thức: văn bản UTF-8, mỗi dòng kết thúc bằng '\n'.
//   tool -> host   "<lệnh> [tham số...]"          vd: "where", "train_start r=8 keys=1,2 hpkey=9 hp=40"
//   host -> tool   "> <kết quả>"                  trả lời đúng một dòng cho mỗi lệnh
//                  "! <sự kiện> <chi tiết>"       sự kiện đẩy ra bất kỳ lúc nào (train, entered_game...)
//
// Mỗi lúc một tool kết nối. Tool ngắt thì server chờ kết nối mới. Chỉ nhận kết nối từ máy này.
// Dòng nhận được gọi LineHandler TRÊN LUỒNG CỦA PIPE: handler nên PostMessage về luồng UI
// rồi mới gọi vào Flash. Send() gọi được từ bất kỳ luồng nào.

#pragma once

#include <windows.h>

#include <atomic>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

class PipeServer {
public:
    using LineHandler = std::function<void(const std::wstring& line)>;

    PipeServer() = default;
    ~PipeServer() { Stop(); }
    PipeServer(const PipeServer&) = delete;
    PipeServer& operator=(const PipeServer&) = delete;

    bool Start(const std::wstring& pipeName, LineHandler onLine);
    void Stop();
    void Send(const std::wstring& line);      // bỏ qua nếu chưa có tool kết nối
    bool HasClient() const { return client_; }
    const std::wstring& Name() const { return name_; }

private:
    void Run();
    bool ServeClient(HANDLE pipe);
    bool WriteAll(HANDLE pipe, const std::string& data, HANDLE ev);

    std::wstring name_;
    LineHandler onLine_;
    std::thread thread_;
    HANDLE stopEv_ = nullptr;
    HANDLE sendEv_ = nullptr;
    std::mutex mu_;
    std::deque<std::string> queue_;
    std::atomic<bool> client_{false};
};
