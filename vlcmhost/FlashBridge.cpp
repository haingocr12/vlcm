#include "FlashBridge.h"

#include <shlobj.h>

#include <cstdlib>
#include <cwctype>

// DIID__IShockwaveFlashEvents
static const GUID DIID_FlashEvents =
    { 0xD27CDB6D, 0xAE6D, 0x11CF, { 0x96, 0xB8, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 } };

static const DISPID DISPID_FLASH_FSCOMMAND = 150;
static const DISPID DISPID_FLASH_FLASHCALL = 197;

// ------------------------------------------------------------------ tiện ích VARIANT/BSTR

static std::wstring VariantToString(const VARIANT& v) {
    VARIANT t;
    VariantInit(&t);
    if (FAILED(VariantChangeType(&t, const_cast<VARIANT*>(&v), 0, VT_BSTR))) return std::wstring();
    std::wstring s = t.bstrVal ? std::wstring(t.bstrVal, SysStringLen(t.bstrVal)) : std::wstring();
    VariantClear(&t);
    return s;
}

static void ClearExcepInfo(EXCEPINFO& ei) {
    SysFreeString(ei.bstrSource);
    SysFreeString(ei.bstrDescription);
    SysFreeString(ei.bstrHelpFile);
    ZeroMemory(&ei, sizeof(ei));
}

// ------------------------------------------------------------------ vòng đời

FlashBridge::~FlashBridge() { Detach(); }

HRESULT FlashBridge::Attach(IUnknown* flashControl) {
    Detach();
    if (!flashControl) return E_POINTER;

    HRESULT hr = flashControl->QueryInterface(IID_IDispatch, reinterpret_cast<void**>(&flash_));
    if (FAILED(hr)) return hr;

    IConnectionPointContainer* cpc = nullptr;
    hr = flashControl->QueryInterface(IID_IConnectionPointContainer, reinterpret_cast<void**>(&cpc));
    if (SUCCEEDED(hr)) {
        hr = cpc->FindConnectionPoint(DIID_FlashEvents, &cp_);
        cpc->Release();
    }
    if (SUCCEEDED(hr)) hr = cp_->Advise(static_cast<IDispatch*>(this), &cookie_);
    if (FAILED(hr)) Detach();
    return hr;
}

void FlashBridge::Detach() {
    if (cp_) {
        if (cookie_) cp_->Unadvise(cookie_);
        cp_->Release();
        cp_ = nullptr;
    }
    cookie_ = 0;
    if (flash_) {
        flash_->Release();
        flash_ = nullptr;
    }
}

// ------------------------------------------------------------------ gọi vào control

HRESULT FlashBridge::PutProperty(LPCOLESTR name, const std::wstring& value) {
    if (!flash_) return E_UNEXPECTED;
    DISPID id = 0;
    LPOLESTR n = const_cast<LPOLESTR>(name);
    HRESULT hr = flash_->GetIDsOfNames(IID_NULL, &n, 1, LOCALE_USER_DEFAULT, &id);
    if (FAILED(hr)) return hr;

    VARIANT arg;
    VariantInit(&arg);
    arg.vt = VT_BSTR;
    arg.bstrVal = SysAllocStringLen(value.data(), static_cast<UINT>(value.size()));
    DISPID named = DISPID_PROPERTYPUT;
    DISPPARAMS dp = { &arg, &named, 1, 1 };
    EXCEPINFO ei;
    ZeroMemory(&ei, sizeof(ei));
    hr = flash_->Invoke(id, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_PROPERTYPUT, &dp, nullptr, &ei, nullptr);
    ClearExcepInfo(ei);
    VariantClear(&arg);
    return hr;
}

HRESULT FlashBridge::CallMethod(LPCOLESTR name, const std::wstring& argText, std::wstring* result) {
    if (!flash_) return E_UNEXPECTED;
    DISPID id = 0;
    LPOLESTR n = const_cast<LPOLESTR>(name);
    HRESULT hr = flash_->GetIDsOfNames(IID_NULL, &n, 1, LOCALE_USER_DEFAULT, &id);
    if (FAILED(hr)) return hr;

    VARIANT arg;
    VariantInit(&arg);
    arg.vt = VT_BSTR;
    arg.bstrVal = SysAllocStringLen(argText.data(), static_cast<UINT>(argText.size()));
    DISPPARAMS dp = { &arg, nullptr, 1, 0 };
    VARIANT res;
    VariantInit(&res);
    EXCEPINFO ei;
    ZeroMemory(&ei, sizeof(ei));
    hr = flash_->Invoke(id, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD, &dp, &res, &ei, nullptr);
    ClearExcepInfo(ei);
    if (SUCCEEDED(hr) && result) *result = VariantToString(res);
    VariantClear(&res);
    VariantClear(&arg);
    return hr;
}

HRESULT FlashBridge::CallFunction(const std::wstring& request, std::wstring* response) {
    return CallMethod(L"CallFunction", request, response);
}

HRESULT FlashBridge::CallString(const std::wstring& fn, const std::vector<std::wstring>& args, std::wstring* result) {
    std::wstring req = L"<invoke name=\"" + XmlEscape(fn) + L"\" returntype=\"xml\"><arguments>";
    for (const auto& a : args) req += L"<string>" + XmlEscape(a) + L"</string>";
    req += L"</arguments></invoke>";
    std::wstring xml;
    HRESULT hr = CallFunction(req, &xml);
    if (FAILED(hr)) return hr;
    if (result) {
        result->clear();
        const std::wstring open = L"<string>", close = L"</string>";
        size_t a = xml.find(open), b = xml.rfind(close);
        if (a != std::wstring::npos && b != std::wstring::npos && b >= a + open.size())
            *result = XmlUnescape(xml.substr(a + open.size(), b - a - open.size()));
    }
    return hr;
}

std::wstring FlashBridge::QueryState() {
    std::wstring xml;
    if (FAILED(CallFunction(L"<invoke name=\"vlcm_state\" returntype=\"xml\"><arguments></arguments></invoke>", &xml)))
        return std::wstring();
    // Kết quả dạng <string>...</string>
    const std::wstring open = L"<string>", close = L"</string>";
    size_t a = xml.find(open), b = xml.rfind(close);
    if (a == std::wstring::npos || b == std::wstring::npos || b < a) return std::wstring();
    return XmlUnescape(xml.substr(a + open.size(), b - a - open.size()));
}

// ------------------------------------------------------------------ sink sự kiện

STDMETHODIMP FlashBridge::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IDispatch) || IsEqualIID(riid, DIID_FlashEvents)) {
        *ppv = static_cast<IDispatch*>(this);
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}

STDMETHODIMP FlashBridge::Invoke(DISPID id, REFIID, LCID, WORD, DISPPARAMS* params,
                                 VARIANT*, EXCEPINFO*, UINT*) {
    if (!params) return S_OK;
    if (id == DISPID_FLASH_FLASHCALL && params->cArgs >= 1) {
        OnFlashCall(VariantToString(params->rgvarg[0]));
    } else if (id == DISPID_FLASH_FSCOMMAND && params->cArgs >= 2) {
        // Tham số COM nằm ngược thứ tự: rgvarg[1] = command, rgvarg[0] = args
        Emit(VariantToString(params->rgvarg[1]), VariantToString(params->rgvarg[0]));
    }
    return S_OK;
}

void FlashBridge::OnFlashCall(const std::wstring& request) {
    InvokeCall inv;
    if (!ParseInvoke(request, &inv)) {
        CallMethod(L"SetReturnValue", L"<null/>", nullptr);
        Emit(L"error", L"FlashCall không đọc được: " + request);
        return;
    }
    // SetReturnValue phải gọi ngay tại đây, trong lúc Flash còn đang chờ.
    if (inv.name == L"vlcm_get_config") {
        CallMethod(L"SetReturnValue", BuildConfigXml(cfg_), nullptr);
    } else if (inv.name == L"vlcm_event") {
        CallMethod(L"SetReturnValue", L"<null/>", nullptr);
        Emit(inv.args.size() > 0 ? inv.args[0] : std::wstring(),
             inv.args.size() > 1 ? inv.args[1] : std::wstring());
    } else if (onCall_) {
        // Lời gọi của game (console.log, setTips, showPopup, window.open...)
        std::wstring ret = onCall_(inv);
        CallMethod(L"SetReturnValue", ret.empty() ? std::wstring(L"<null/>") : ret, nullptr);
    } else {
        CallMethod(L"SetReturnValue", L"<null/>", nullptr);
        Emit(L"unknown_call", inv.name);
    }
}

void FlashBridge::Emit(const std::wstring& name, const std::wstring& detail) {
    if (onEvent_) onEvent_(name, detail);
}

// ------------------------------------------------------------------ XML

std::wstring FlashBridge::XmlEscape(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size() + 16);
    for (wchar_t c : s) {
        switch (c) {
        case L'&':  out += L"&amp;";  break;
        case L'<':  out += L"&lt;";   break;
        case L'>':  out += L"&gt;";   break;
        case L'"':  out += L"&quot;"; break;
        case L'\'': out += L"&apos;"; break;
        default:    out += c;
        }
    }
    return out;
}

static void AppendCodePoint(std::wstring& out, unsigned long cp) {
    if (cp >= 0x10000 && cp <= 0x10FFFF) {
        cp -= 0x10000;
        out += static_cast<wchar_t>(0xD800 + (cp >> 10));
        out += static_cast<wchar_t>(0xDC00 + (cp & 0x3FF));
    } else {
        out += static_cast<wchar_t>(cp);
    }
}

std::wstring FlashBridge::XmlUnescape(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != L'&') { out += s[i]; continue; }
        size_t semi = s.find(L';', i);
        if (semi == std::wstring::npos || semi - i > 10) { out += s[i]; continue; }
        std::wstring ent = s.substr(i + 1, semi - i - 1);
        if (ent == L"amp") out += L'&';
        else if (ent == L"lt") out += L'<';
        else if (ent == L"gt") out += L'>';
        else if (ent == L"quot") out += L'"';
        else if (ent == L"apos") out += L'\'';
        else if (ent.size() > 1 && ent[0] == L'#') {
            bool hex = ent[1] == L'x' || ent[1] == L'X';
            AppendCodePoint(out, std::wcstoul(ent.c_str() + (hex ? 2 : 1), nullptr, hex ? 16 : 10));
        } else { out += s[i]; continue; }
        i = semi;
    }
    return out;
}

// Đọc một thẻ bắt đầu tại pos (s[pos] == '<'). Trả tên thẻ, cờ thẻ đóng / tự đóng, và vị trí sau '>'.
static bool ReadTag(const std::wstring& s, size_t pos, std::wstring* name, bool* closing, bool* selfClosing, size_t* next) {
    size_t gt = s.find(L'>', pos);
    if (gt == std::wstring::npos) return false;
    std::wstring body = s.substr(pos + 1, gt - pos - 1);
    *closing = !body.empty() && body[0] == L'/';
    *selfClosing = !body.empty() && body[body.size() - 1] == L'/';
    size_t start = *closing ? 1 : 0, end = start;
    while (end < body.size() && !std::iswspace(body[end]) && body[end] != L'/') ++end;
    *name = body.substr(start, end - start);
    *next = gt + 1;
    return true;
}

bool FlashBridge::ParseInvoke(const std::wstring& x, InvokeCall* out) {
    out->name.clear();
    out->args.clear();

    size_t p = x.find(L"<invoke");
    if (p == std::wstring::npos) return false;
    size_t q = x.find(L"name=\"", p);
    if (q == std::wstring::npos) return false;
    q += 6;
    size_t e = x.find(L'"', q);
    if (e == std::wstring::npos) return false;
    out->name = XmlUnescape(x.substr(q, e - q));

    size_t a = x.find(L"<arguments", e);
    if (a == std::wstring::npos) return true;
    std::wstring tag;
    bool closing = false, selfClosing = false;
    if (!ReadTag(x, a, &tag, &closing, &selfClosing, &p)) return false;
    if (selfClosing) return true;

    for (;;) {
        while (p < x.size() && std::iswspace(x[p])) ++p;
        if (p >= x.size() || x[p] != L'<') return false;
        size_t after = 0;
        if (!ReadTag(x, p, &tag, &closing, &selfClosing, &after)) return false;
        if (closing) return tag == L"arguments";

        if (selfClosing) {                         // <true/> <false/> <null/> <undefined/> <string/>
            out->args.push_back(tag == L"true" || tag == L"false" ? tag : std::wstring());
            p = after;
            continue;
        }
        if (tag == L"string" || tag == L"number") {
            const std::wstring close = L"</" + tag + L">";
            size_t end = x.find(close, after);
            if (end == std::wstring::npos) return false;
            out->args.push_back(XmlUnescape(x.substr(after, end - after)));
            p = end + close.size();
            continue;
        }
        // <object>, <array>: không dùng tới, bỏ qua cả khối (đếm lồng nhau theo cùng tên thẻ)
        int depth = 1;
        p = after;
        while (depth > 0) {
            p = x.find(L'<', p);
            if (p == std::wstring::npos) return false;
            std::wstring t;
            bool c = false, sc = false;
            if (!ReadTag(x, p, &t, &c, &sc, &p)) return false;
            if (t == tag && !sc) depth += c ? -1 : 1;
        }
        out->args.push_back(std::wstring());
    }
}

std::wstring FlashBridge::BuildConfigXml(const Config& cfg) {
    auto str = [](const wchar_t* id, const std::wstring& v) {
        return std::wstring(L"<property id=\"") + id + L"\"><string>" + XmlEscape(v) + L"</string></property>";
    };
    auto num = [](const wchar_t* id, int v) {
        return std::wstring(L"<property id=\"") + id + L"\"><number>" + std::to_wstring(v) + L"</number></property>";
    };
    return L"<object>" + str(L"url", cfg.url) + str(L"query", cfg.query) + num(L"line", cfg.line)
         + str(L"charName", cfg.charName) + num(L"serverId", cfg.serverId) + L"</object>";
}

// ------------------------------------------------------------------ FlashPlayerTrust

// Ghi %APPDATA%\Macromedia\Flash Player\#Security\FlashPlayerTrust\<fileName> chứa trustedPath
// (một thư mục hoặc một file). SWF nằm trong đó chạy ở sandbox localTrusted: tải được mạng
// và gọi được vào nội dung từ domain khác, không phụ thuộc allowDomain của game.
bool FlashBridge::WriteTrustFile(const std::wstring& trustedPath, const std::wstring& fileName) {
    wchar_t appData[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, appData))) return false;
    std::wstring dir = std::wstring(appData) + L"\\Macromedia\\Flash Player\\#Security\\FlashPlayerTrust";
    int rc = SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    if (rc != ERROR_SUCCESS && rc != ERROR_ALREADY_EXISTS && rc != ERROR_FILE_EXISTS) return false;

    std::wstring line = trustedPath + L"\r\n";
    int n = WideCharToMultiByte(CP_UTF8, 0, line.c_str(), static_cast<int>(line.size()), nullptr, 0, nullptr, nullptr);
    std::string utf8(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, line.c_str(), static_cast<int>(line.size()), &utf8[0], n, nullptr, nullptr);

    HANDLE h = CreateFileW((dir + L"\\" + fileName).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    BOOL ok = WriteFile(h, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    CloseHandle(h);
    return ok && written == utf8.size();
}
