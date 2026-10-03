// FlashBridge: cầu nối ExternalInterface giữa host C++ và VlcmLoader.swf chạy trong Flash.ocx.
//
// Không phụ thuộc ATL hay #import flash.ocx: mọi thao tác với control đi qua IDispatch theo tên,
// sự kiện nhận qua connection point _IShockwaveFlashEvents. Build được bằng MSVC và MinGW.
//
// Cách dùng:
//
//     // 1. TRƯỚC khi tạo control Flash (Flash có thể chỉ đọc FlashPlayerTrust lúc khởi tạo):
//     FlashBridge::WriteTrustFile(L"C:\\...\\thư mục chứa VlcmLoader.swf");   // tùy chọn, xem .cpp
//
//     // 2. Tạo control, rồi trước khi nạp SWF:
//     FlashBridge bridge;                       // phải sống lâu hơn control, hoặc gọi Detach() trước khi hủy control
//     FlashBridge::Config cfg;
//     cfg.url      = L"https://.../program526/game/TGameLoader.swf?ver=...&config=...&auth=...&sign=...";
//     cfg.line     = 2;                         // 0 = để người chơi tự chọn kênh
//     cfg.charName = L"King21";                 // rỗng = để người chơi tự chọn nhân vật; "[x]King21" vẫn khớp
//     cfg.serverId = 0;                         // tùy chọn
//     bridge.SetConfig(cfg);
//     bridge.SetEventHandler([](const std::wstring& name, const std::wstring& detail) { ... });
//     bridge.Attach(pFlashUnknown);             // IUnknown của control (vd CAxWindow::QueryControl)
//     bridge.PutProperty(L"AllowScriptAccess", L"always");
//     bridge.PutProperty(L"Movie", L"C:\\...\\VlcmLoader.swf");
//
// Link: MSVC tự link qua pragma bên dưới; MinGW thêm -lole32 -loleaut32 -luuid -lshell32.
//
// Mọi callback chạy trên luồng UI (STA) của control, bên trong lời gọi ExternalInterface của Flash:
// đừng chặn lâu và đừng hủy control trong handler.

#pragma once

#ifdef _MSC_VER
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "shell32.lib")
#endif

#include <windows.h>
#include <ole2.h>
#include <ocidl.h>

#include <functional>
#include <string>
#include <vector>

class FlashBridge : public IDispatch {
public:
    struct Config {
        std::wstring url;       // link TGameLoader.swf, đã gồm query
        std::wstring query;     // tùy chọn: nối thêm vào url
        std::wstring charName;
        int line = 0;
        int serverId = 0;
    };

    // name: status, classes, line_not_found, char_not_found, entered_game, disconnected, error
    //       (hoặc lệnh fscommand nếu SWF dùng fscommand). detail: xem đầu VlcmLoader.as.
    using EventHandler = std::function<void(const std::wstring& name, const std::wstring& detail)>;

    struct InvokeCall {
        std::wstring name;
        std::vector<std::wstring> args;   // string/number giữ nguyên chữ; true/false; null/undefined -> ""
    };

    // Lời gọi ExternalInterface.call không phải của VlcmLoader (vd của game: console.log, setTips,
    // showPopup, window.open). Trả XML kết quả (<null/>, <string>..</string>...); rỗng = <null/>.
    // Chạy bên trong FlashCall: đừng chặn lâu và đừng hủy control trong handler.
    using CallHandler = std::function<std::wstring(const InvokeCall& call)>;

    FlashBridge() = default;
    ~FlashBridge();
    FlashBridge(const FlashBridge&) = delete;
    FlashBridge& operator=(const FlashBridge&) = delete;

    HRESULT Attach(IUnknown* flashControl);
    void Detach();

    void SetConfig(const Config& cfg) { cfg_ = cfg; }
    void SetEventHandler(EventHandler h) { onEvent_ = std::move(h); }
    void SetCallHandler(CallHandler h) { onCall_ = std::move(h); }

    HRESULT PutProperty(LPCOLESTR name, const std::wstring& value);

    // Gọi hàm SWF đã addCallback. request là XML <invoke ...>; response nhận XML kết quả.
    HRESULT CallFunction(const std::wstring& request, std::wstring* response);
    // Gọi hàm SWF đã addCallback với các tham số chuỗi; kết quả dạng chuỗi (rỗng nếu SWF trả null).
    // Lỗi (hàm chưa addCallback, Flash chưa nạp xong) trả HRESULT thất bại.
    HRESULT CallString(const std::wstring& fn, const std::vector<std::wstring>& args, std::wstring* result);
    // Tiện ích: gọi vlcm_state(), trả chuỗi trạng thái (rỗng nếu lỗi).
    std::wstring QueryState();

    // Ghi file FlashPlayerTrust để thư mục chứa SWF chạy ở sandbox localTrusted.
    static bool WriteTrustFile(const std::wstring& trustedPath, const std::wstring& fileName = L"vlcmhost.cfg");

    static std::wstring XmlEscape(const std::wstring& s);
    static std::wstring XmlUnescape(const std::wstring& s);
    static bool ParseInvoke(const std::wstring& xml, InvokeCall* out);
    static std::wstring BuildConfigXml(const Config& cfg);

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override { return 2; }    // đối tượng do host sở hữu, không tự xóa
    STDMETHODIMP_(ULONG) Release() override { return 1; }
    // IDispatch (sink sự kiện)
    STDMETHODIMP GetTypeInfoCount(UINT* n) override { if (n) *n = 0; return S_OK; }
    STDMETHODIMP GetTypeInfo(UINT, LCID, ITypeInfo**) override { return E_NOTIMPL; }
    STDMETHODIMP GetIDsOfNames(REFIID, LPOLESTR*, UINT, LCID, DISPID*) override { return E_NOTIMPL; }
    STDMETHODIMP Invoke(DISPID id, REFIID riid, LCID lcid, WORD flags, DISPPARAMS* params,
                        VARIANT* result, EXCEPINFO* excep, UINT* argErr) override;

private:
    void OnFlashCall(const std::wstring& request);
    HRESULT CallMethod(LPCOLESTR name, const std::wstring& arg, std::wstring* result);
    void Emit(const std::wstring& name, const std::wstring& detail);

    IDispatch* flash_ = nullptr;
    IConnectionPoint* cp_ = nullptr;
    DWORD cookie_ = 0;
    Config cfg_;
    EventHandler onEvent_;
    CallHandler onCall_;
};
