#include "WebViewBridge.h"
#include "MonitorManager.h"
#include <sstream>
#include <algorithm>
#include <cwctype>
#include <cstdarg>

namespace {

    std::string WideToUtf8(const std::wstring& w) {
        if (w.empty()) return std::string();
        int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                                      nullptr, 0, nullptr, nullptr);
        if (len <= 0) return std::string();
        std::string out(len, '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                            &out[0], len, nullptr, nullptr);
        return out;
    }

    void BridgeLog(const WCHAR* fmt, ...) {
#ifdef _DEBUG
        WCHAR path[MAX_PATH];
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        std::wstring logPath(path);
        size_t lastSlash = logPath.find_last_of(L"\\/");
        if (lastSlash != std::wstring::npos)
            logPath = logPath.substr(0, lastSlash);
        logPath += L"\\debug.log";

        WCHAR buf[512];
        va_list args;
        va_start(args, fmt);
        _vsnwprintf_s(buf, _TRUNCATE, fmt, args);
        va_end(args);

        HANDLE hFile = CreateFileW(logPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ,
                                   nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile != INVALID_HANDLE_VALUE) {
            std::string line = WideToUtf8(buf);
            line += "\r\n";
            DWORD written = 0;
            WriteFile(hFile, line.c_str(), static_cast<DWORD>(line.size()), &written, nullptr);
            CloseHandle(hFile);
        }
#else
        (void)fmt;
#endif
    }

    // ---- DDC/CI packet hex computation ----

    std::wstring PacketHex(const std::vector<uint8_t>& bytes) {
        std::wostringstream ss;
        for (size_t i = 0; i < bytes.size(); i++) {
            if (i > 0) ss << L' ';
            WCHAR h[4];
            swprintf_s(h, L"%02X", bytes[i]);
            ss << h;
        }
        return ss.str();
    }

    uint8_t ChkXor(const std::vector<uint8_t>& bytes) {
        uint8_t c = 0;
        for (auto b : bytes) c ^= b;
        return c;
    }

    std::wstring BuildVCPGetSendHex(uint8_t vcpCode) {
        std::vector<uint8_t> p = {0x6E, 0x51, 0x82, 0x01, vcpCode};
        p.push_back(ChkXor(p));
        return PacketHex(p);
    }

    std::wstring BuildVCPGetRecvHex(uint8_t vcpCode, uint32_t current, uint32_t max) {
        std::vector<uint8_t> p = {0x6E, 0x51, 0x87, 0x00, vcpCode, 0x00,
            static_cast<uint8_t>((max >> 8) & 0xFF),
            static_cast<uint8_t>(max & 0xFF),
            static_cast<uint8_t>((current >> 8) & 0xFF),
            static_cast<uint8_t>(current & 0xFF)};
        p.push_back(ChkXor(p));
        return PacketHex(p);
    }

    std::wstring BuildVCPSetSendHex(uint8_t vcpCode, uint32_t value) {
        std::vector<uint8_t> p = {0x6E, 0x51, 0x84, 0x03, vcpCode,
            static_cast<uint8_t>((value >> 8) & 0xFF),
            static_cast<uint8_t>(value & 0xFF)};
        p.push_back(ChkXor(p));
        return PacketHex(p);
    }

    std::wstring BuildVCPSetRecvHex() {
        std::vector<uint8_t> p = {0x6E, 0x51, 0x81, 0x00};
        p.push_back(ChkXor(p));
        return PacketHex(p);
    }

    std::wstring BuildCapsSendHex(uint16_t offset) {
        std::vector<uint8_t> p = {0x6E, 0x51, 0x83, 0xF3,
            static_cast<uint8_t>((offset >> 8) & 0xFF),
            static_cast<uint8_t>(offset & 0xFF)};
        p.push_back(ChkXor(p));
        return PacketHex(p);
    }

    std::wstring BuildCapsSendHex() {
        return BuildCapsSendHex(0);
    }

    // Build per-segment recv hex for segmented capabilities log.
    // offset = our request offset, nextOffset = offset + chunk.size(), chunk = data bytes
    static const size_t CAPS_CHUNK_SIZE = 26; // max data bytes per I2C segment

    std::wstring BuildCapsSegRecvHex(uint16_t offset, uint16_t nextOffset, const std::string& chunk) {
        std::vector<uint8_t> p = {0x6E, 0x51};
        uint32_t dataLen = 3 + static_cast<uint32_t>(chunk.size());
        p.push_back(static_cast<uint8_t>(0x80 | (dataLen & 0x7F)));
        p.push_back(0x00); // result: success
        p.push_back(static_cast<uint8_t>((nextOffset >> 8) & 0xFF));
        p.push_back(static_cast<uint8_t>(nextOffset & 0xFF));
        for (char ch : chunk)
            p.push_back(static_cast<uint8_t>(ch));
        p.push_back(ChkXor(p));
        return PacketHex(p);
    }

    // Full capabilities read as it actually goes over the wire: one I2C reply
    // packet per CAPS_CHUNK_SIZE chunk (a single DDC packet's length field is
    // 7-bit, so the whole string never fits in one packet). Segments are joined
    // by "  " for display.
    std::wstring BuildCapsRecvHex(const std::string& capsStr) {
        if (capsStr.empty()) return L"";
        std::wstring out;
        size_t offset = 0;
        while (offset < capsStr.size()) {
            size_t chunkSize = (std::min)(CAPS_CHUNK_SIZE, capsStr.size() - offset);
            std::string chunk = capsStr.substr(offset, chunkSize);
            uint16_t nextOffset = static_cast<uint16_t>(offset + chunkSize);
            if (!out.empty()) out += L"  ";
            out += BuildCapsSegRecvHex(static_cast<uint16_t>(offset), nextOffset, chunk);
            offset += chunkSize;
        }
        return out;
    }

    std::wstring BuildCapsSegmentsJson(const std::string& capsStr, size_t totalLen) {
        size_t dataLen = capsStr.empty() ? totalLen : capsStr.size();
        if (dataLen == 0) return L"[]";
        std::wostringstream ss;
        ss << L"[";
        size_t offset = 0;
        bool first = true;
        while (offset < dataLen) {
            size_t chunkSize = (std::min)(CAPS_CHUNK_SIZE, dataLen - offset);
            if (!first) ss << L",";
            first = false;
            ss << L"{\"sendHex\":\"" << BuildCapsSendHex(static_cast<uint16_t>(offset)) << L"\"";
            if (!capsStr.empty()) {
                std::string chunk = capsStr.substr(offset, chunkSize);
                uint16_t nextOffset = static_cast<uint16_t>(offset + chunkSize);
                ss << L",\"recvHex\":\"" << BuildCapsSegRecvHex(static_cast<uint16_t>(offset), nextOffset, chunk) << L"\"";
            } else {
                ss << L",\"recvHex\":\"\"";
            }
            ss << L"}";
            offset += chunkSize;
        }
        ss << L"]";
        return ss.str();
    }


    int ExtractJsonInt(const std::wstring& json, const std::wstring& key) {
        std::wstring search = L"\"" + key + L"\":";
        size_t pos = json.find(search);
        if (pos == std::wstring::npos) return -1;
        pos += search.length();
        while (pos < json.length() && std::iswspace(static_cast<wint_t>(json[pos]))) pos++;
        size_t end = pos;
        while (end < json.length() && (std::iswdigit(static_cast<wint_t>(json[end])) || json[end] == L'-')) end++;
        if (end == pos) return -1;
        return std::stoi(json.substr(pos, end - pos));
    }

    std::wstring ExtractJsonString(const std::wstring& json, const std::wstring& key) {
        std::wstring search = L"\"" + key + L"\":";
        size_t pos = json.find(search);
        if (pos == std::wstring::npos) return L"";
        pos += search.length();
        while (pos < json.length() && std::iswspace(static_cast<wint_t>(json[pos]))) pos++;
        if (pos >= json.length() || json[pos] != L'"') return L"";
        pos++;
        size_t end = json.find(L'"', pos);
        if (end == std::wstring::npos) return L"";
        return json.substr(pos, end - pos);
    }
}

WebViewBridge::WebViewBridge(MonitorManager* monitorMgr)
    : m_monitorMgr(monitorMgr) {}

WebViewBridge::~WebViewBridge() {
    Close();
}

void WebViewBridge::Close() {
    if (m_webview) {
        if (m_webMessageToken.value) {
            m_webview->remove_WebMessageReceived(m_webMessageToken);
            m_webMessageToken.value = 0;
        }
        if (m_navCompleteToken.value) {
            m_webview->remove_NavigationCompleted(m_navCompleteToken);
            m_navCompleteToken.value = 0;
        }
    }
    if (m_controller) {
        m_controller->Close();
        m_controller = nullptr;
    }
    m_webview = nullptr;
}

std::wstring WebViewBridge::GetWebDirPath() {
    WCHAR path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring dir(path);
    size_t lastSlash = dir.find_last_of(L"\\/");
    if (lastSlash != std::wstring::npos)
        dir = dir.substr(0, lastSlash);
    return dir + L"\\web";
}

HRESULT WebViewBridge::Initialize(HWND hwnd) {
    m_hwnd = hwnd;

    return CreateCoreWebView2EnvironmentWithOptions(
        nullptr, nullptr, nullptr,
        Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this, hwnd](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                return OnEnvironmentCreated(hwnd, result, env);
            }).Get());
}

HRESULT WebViewBridge::OnEnvironmentCreated(HWND hwnd, HRESULT result, ICoreWebView2Environment* env) {
    if (FAILED(result) || !env) {
        WCHAR msg[256];
        swprintf_s(msg, L"WebView2 environment creation failed.\nHRESULT: 0x%08X\n\n"
                  L"Please install the WebView2 Runtime:\n"
                  L"https://go.microsoft.com/fwlink/p/?LinkId=2124703",
                  static_cast<unsigned>(result));
        MessageBoxW(hwnd, msg, L"DDCCI Tool - WebView2 Error", MB_ICONERROR);
        return result;
    }

    return env->CreateCoreWebView2Controller(
        hwnd,
        Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
            [this](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
                return OnControllerCreated(result, controller);
            }).Get());
}

HRESULT WebViewBridge::OnControllerCreated(HRESULT result, ICoreWebView2Controller* controller) {
    if (FAILED(result) || !controller) {
        MessageBoxW(m_hwnd, L"WebView2 controller creation failed.\n\n"
                    L"Please ensure the WebView2 Runtime is installed.",
                    L"DDCCI Tool - WebView2 Error", MB_ICONERROR);
        return result;
    }

    m_controller = controller;
    m_controller->get_CoreWebView2(&m_webview);

    if (!m_webview) return E_FAIL;

    RECT rect;
    GetClientRect(m_hwnd, &rect);
    m_controller->put_Bounds(rect);
    m_controller->put_IsVisible(TRUE);

    // Set background color to match dark theme (avoid white flash)
    Microsoft::WRL::ComPtr<ICoreWebView2Controller2> controller2;
    if (SUCCEEDED(m_controller.As(&controller2))) {
        COREWEBVIEW2_COLOR bgColor = {255, 0x1E, 0x1E, 0x2E}; // #1E1E2E
        controller2->put_DefaultBackgroundColor(bgColor);
    }

    Microsoft::WRL::ComPtr<ICoreWebView2Settings> settings;
    m_webview->get_Settings(&settings);
    if (settings) {
        settings->put_AreDefaultScriptDialogsEnabled(FALSE);
#ifdef _DEBUG
        settings->put_AreDevToolsEnabled(TRUE);
#else
        settings->put_AreDevToolsEnabled(FALSE);
#endif
    }

    // Register WebMessageReceived handler
    m_webview->add_WebMessageReceived(
        Microsoft::WRL::Callback<ICoreWebView2WebMessageReceivedEventHandler>(
            [this](ICoreWebView2* sender, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                return OnWebMessageReceived(sender, args);
            }).Get(),
        &m_webMessageToken);

    // Register NavigationCompleted handler for diagnostics
    m_webview->add_NavigationCompleted(
        Microsoft::WRL::Callback<ICoreWebView2NavigationCompletedEventHandler>(
            [this](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                BOOL success = FALSE;
                if (args) args->get_IsSuccess(&success);
                if (!success) {
                    COREWEBVIEW2_WEB_ERROR_STATUS status = COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN;
                    args->get_WebErrorStatus(&status);
                    WCHAR msg[256];
                    swprintf_s(msg, L"Page load failed. Error status: %u.\n\n"
                              L"Please ensure the web/ folder is in the same directory as the EXE.",
                              static_cast<unsigned>(status));
                    MessageBoxW(m_hwnd, msg, L"DDCCI Tool - Load Error", MB_ICONWARNING);
                }
                return S_OK;
            }).Get(),
        &m_navCompleteToken);

    // Try virtual host mapping first (allows https:// which is required for chrome.webview API)
    auto webDir = GetWebDirPath();
    HRESULT hrMap = E_FAIL;
    Microsoft::WRL::ComPtr<ICoreWebView2_3> webview3;
    if (SUCCEEDED(m_webview.As(&webview3))) {
        hrMap = webview3->SetVirtualHostNameToFolderMapping(
            L"ddcci.app", webDir.c_str(),
            COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
        if (SUCCEEDED(hrMap)) {
            return m_webview->Navigate(L"https://ddcci.app/index.html");
        }
    }

    // Fallback 1: try file:// with forward slashes
    for (auto& ch : webDir) { if (ch == L'\\') ch = L'/'; }
    std::wstring fileUrl = L"file:///" + webDir + L"/index.html";
    HRESULT hrNav = m_webview->Navigate(fileUrl.c_str());
    if (SUCCEEDED(hrNav)) return hrNav;

    // Fallback 2: inline diagnostic page
    WCHAR fallbackHtml[1024];
    swprintf_s(fallbackHtml,
        L"<html><body style='background:#1e1e2e;color:#e0e0f0;font-family:sans-serif;"
        L"display:flex;align-items:center;justify-content:center;height:100vh;margin:0'>"
        L"<div style='text-align:center'><h1>DDCCI Tool</h1>"
        L"<p>WebView2 is working, but the UI page failed to load.</p>"
        L"<p style='color:#a0a0c0;font-size:13px'>"
        L"VirtualHostMapping: 0x%08X<br>FileURL: 0x%08X<br>"
        L"WebDir: %s</p></div></body></html>",
        static_cast<unsigned>(hrMap), static_cast<unsigned>(hrNav),
        GetWebDirPath().c_str());
    return m_webview->NavigateToString(fallbackHtml);
}

HRESULT WebViewBridge::OnWebMessageReceived(ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) {
    if (!args || !m_webview) return E_INVALIDARG;

    LPWSTR rawMessage = nullptr;
    HRESULT hr = args->TryGetWebMessageAsString(&rawMessage);
    if (FAILED(hr) || !rawMessage) return hr;

    BridgeLog(L"=== C++ received: %s", rawMessage);

    std::wstring response = HandleRequest(rawMessage);
    CoTaskMemFree(rawMessage);

    BridgeLog(L"C++ response: %s", response.c_str());

    if (!response.empty()) {
        // Use ExecuteScript instead of PostWebMessageAsJson for reliability.
        // PostWebMessageAsJson sometimes fails to trigger the JS 'message' event.
        std::wstring script = L"window.__bridgeReceive(" + response + L");";
        m_webview->ExecuteScript(script.c_str(), nullptr);
    }

    return S_OK;
}

void WebViewBridge::Resize() {
    if (m_controller) {
        RECT rect;
        GetClientRect(m_hwnd, &rect);
        m_controller->put_Bounds(rect);
    }
}

// ---- JSON request dispatch ----

std::wstring WebViewBridge::HandleRequest(const std::wstring& json) {
    std::wstring method = ExtractJsonString(json, L"method");

    if (method == L"enumerateMonitors") {
        return BuildMonitorList();
    }

    if (method == L"getCapabilities") {
        int monitorIndex = ExtractJsonInt(json, L"monitor");
        if (monitorIndex < 0) return BuildError(L"Missing monitor index");
        return BuildGetCapabilitiesResponse(monitorIndex);
    }

    if (method == L"getVCP") {
        int monitorIndex = ExtractJsonInt(json, L"monitor");
        int vcpCode = ExtractJsonInt(json, L"vcpCode");
        if (monitorIndex < 0 || vcpCode < 0) return BuildError(L"Missing parameters");
        return BuildGetVCPResponse(monitorIndex, static_cast<uint8_t>(vcpCode));
    }

    if (method == L"setVCP") {
        int monitorIndex = ExtractJsonInt(json, L"monitor");
        int vcpCode = ExtractJsonInt(json, L"vcpCode");
        int value = ExtractJsonInt(json, L"value");
        if (monitorIndex < 0 || vcpCode < 0) return BuildError(L"Missing parameters");
        return BuildSetVCPResponse(monitorIndex, static_cast<uint8_t>(vcpCode), value);
    }

    if (method == L"sendRaw") {
        int monitorIndex = ExtractJsonInt(json, L"monitor");
        std::wstring bodyHex = ExtractJsonString(json, L"bodyHex");
        if (monitorIndex < 0 || bodyHex.empty()) return BuildError(L"Missing parameters");
        return BuildRawCommandResponse(monitorIndex, bodyHex);
    }

    return BuildError(L"Unknown method: " + method);
}

// ---- Response builders ----

std::wstring WebViewBridge::EscapeJson(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.length() + 10);
    for (wchar_t ch : s) {
        switch (ch) {
            case L'"':  out += L"\\\""; break;
            case L'\\': out += L"\\\\"; break;
            case L'\n': out += L"\\n";  break;
            case L'\r': out += L"\\r";  break;
            case L'\t': out += L"\\t";  break;
            default:    out += ch;      break;
        }
    }
    return out;
}

std::wstring WebViewBridge::BuildError(const std::wstring& msg) {
    std::wostringstream ss;
    ss << L"{\"id\":null,\"error\":\"" << EscapeJson(msg) << L"\"}";
    return ss.str();
}

std::wstring WebViewBridge::BuildMonitorList() {
    std::wostringstream ss;
    ss << L"{\"type\":\"monitorList\",\"monitors\":[";
    int count = m_monitorMgr->GetMonitorCount();
    for (int i = 0; i < count; i++) {
        if (i > 0) ss << L",";
        auto* mon = m_monitorMgr->GetMonitor(i);
        ss << L"{\"index\":" << i
           << L",\"name\":\"" << EscapeJson(mon ? mon->name : L"Unknown") << L"\"}";
    }
    ss << L"]}";
    return ss.str();
}

std::wstring WebViewBridge::BuildGetCapabilitiesResponse(int monitorIndex) {
    auto caps = m_monitorMgr->GetCapabilities(monitorIndex);
    std::string capsStr = WideToUtf8(caps);
    auto vcpCodes = MonitorManager::ParseSupportedVCPCodes(caps);

    // Even when CapabilitiesRequestAndCapabilitiesReply fails (capsStr empty),
    // estimate segment count from the expected length so that multi-segment
    // TX packets appear in the log instead of nothing.
    size_t totalLen = capsStr.size();
    std::wstring recvHex;
    if (!capsStr.empty()) {
        recvHex = BuildCapsRecvHex(capsStr);
    } else {
        DWORD expectedLen = m_monitorMgr->GetCapabilitiesStringLen(monitorIndex);
        if (expectedLen > 0) totalLen = expectedLen;
        recvHex = L"";
    }

    std::wostringstream ss;
    ss << L"{\"type\":\"capabilities\",\"monitor\":" << monitorIndex
       << L",\"capabilities\":\"" << EscapeJson(caps) << L"\""
       << L",\"supportedVCP\":[";
    for (size_t i = 0; i < vcpCodes.size(); i++) {
        if (i > 0) ss << L",";
        ss << static_cast<int>(vcpCodes[i]);
    }
    ss << L"]"
       << L",\"segments\":" << BuildCapsSegmentsJson(capsStr, totalLen)
       << L",\"sendHex\":\"" << BuildCapsSendHex() << L"\""
       << L",\"recvHex\":\"" << recvHex << L"\""
       << L"}";
    return ss.str();
}

std::wstring WebViewBridge::BuildGetVCPResponse(int monitorIndex, uint8_t vcpCode) {
    auto feature = m_monitorMgr->GetVCPFeature(monitorIndex, vcpCode);
    std::wostringstream ss;
    ss << L"{\"type\":\"vcpFeature\",\"monitor\":" << monitorIndex
       << L",\"vcpCode\":" << static_cast<int>(vcpCode)
       << L",\"current\":" << feature.current
       << L",\"max\":" << feature.max
       << L",\"valid\":" << (feature.valid ? L"true" : L"false")
       << L",\"sendHex\":\"" << BuildVCPGetSendHex(vcpCode) << L"\""
       << L",\"recvHex\":\"" << BuildVCPGetRecvHex(vcpCode, feature.current, feature.max) << L"\""
       << L"}";
    return ss.str();
}

std::wstring WebViewBridge::BuildSetVCPResponse(int monitorIndex, uint8_t vcpCode, uint32_t value) {
    m_monitorMgr->SetVCP(monitorIndex, vcpCode, value);
    std::wostringstream ss;
    ss << L"{\"type\":\"vcpSet\",\"monitor\":" << monitorIndex
       << L",\"vcpCode\":" << static_cast<int>(vcpCode)
       << L",\"value\":" << value
       << L",\"sendHex\":\"" << BuildVCPSetSendHex(vcpCode, value) << L"\""
       << L",\"recvHex\":\"" << BuildVCPSetRecvHex() << L"\""
       << L"}";
    return ss.str();
}

// ---- Raw command ----

static bool ParseHexByte(const WCHAR*& p, uint8_t& out) {
    // Skip spaces
    while (*p == L' ') p++;
    if (!*p) return false;

    uint8_t val = 0;
    for (int i = 0; i < 2; i++) {
        WCHAR ch = *p;
        if (ch >= L'0' && ch <= L'9')      val = (val << 4) | (ch - L'0');
        else if (ch >= L'A' && ch <= L'F') val = (val << 4) | (ch - L'A' + 10);
        else if (ch >= L'a' && ch <= L'f') val = (val << 4) | (ch - L'a' + 10);
        else return false;
        p++;
    }
    out = val;
    return true;
}

static std::vector<uint8_t> ParseHexString(const std::wstring& hex) {
    std::vector<uint8_t> bytes;
    const WCHAR* p = hex.c_str();
    while (*p) {
        while (*p == L' ') p++;
        if (!*p) break;
        uint8_t b;
        if (ParseHexByte(p, b))
            bytes.push_back(b);
        else
            break;
    }
    return bytes;
}

static std::wstring BytesToHexStr(const std::vector<uint8_t>& bytes) {
    std::wostringstream ss;
    for (size_t i = 0; i < bytes.size(); i++) {
        if (i > 0) ss << L' ';
        WCHAR h[4];
        swprintf_s(h, L"%02X", bytes[i]);
        ss << h;
    }
    return ss.str();
}

std::wstring WebViewBridge::BuildRawCommandResponse(int monitorIndex, const std::wstring& bodyHex) {
    auto* mon = m_monitorMgr->GetMonitor(monitorIndex);
    if (!mon) return BuildError(L"Monitor not found");

    std::vector<uint8_t> body = ParseHexString(bodyHex);
    if (body.empty()) return BuildError(L"Invalid hex body");

    // Build full TX packet: [6E] [51] [LEN=0x80|body.size()] [body...] [CHK]
    std::vector<uint8_t> tx = {0x6E, 0x51};
    tx.push_back(static_cast<uint8_t>(0x80 | (body.size() & 0x7F)));
    for (auto b : body) tx.push_back(b);
    tx.push_back(ChkXor(tx));

    std::wstring txHex = BytesToHexStr(tx);
    std::wstring rxHex, parseInfo;

    if (body.size() >= 2 && body[0] == 0x01) {
        // GetVCPFeature
        uint8_t vcp = body[1];
        auto feat = m_monitorMgr->GetVCPFeature(monitorIndex, vcp);
        rxHex = BuildVCPGetRecvHex(vcp, feat.current, feat.max);

        std::wostringstream info;
        info << L"GetVCP " << std::hex << std::uppercase << static_cast<int>(vcp)
             << L": current=" << std::dec << feat.current
             << L", max=" << feat.max;
        parseInfo = info.str();

    } else if (body.size() >= 4 && body[0] == 0x03) {
        // SetVCPFeature
        uint8_t vcp = body[1];
        uint32_t value = (static_cast<uint32_t>(body[2]) << 8) | body[3];
        m_monitorMgr->SetVCP(monitorIndex, vcp, value);
        rxHex = BuildVCPSetRecvHex();

        std::wostringstream info;
        info << L"SetVCP " << std::hex << std::uppercase << static_cast<int>(vcp)
             << L" = " << std::dec << value;
        parseInfo = info.str();

    } else if (body.size() >= 3 && body[0] == 0xF3) {
        // Capabilities request (offset in bytes 1-3)
        auto caps = m_monitorMgr->GetCapabilities(monitorIndex);
        std::string capsStr = WideToUtf8(caps);
        rxHex = BuildCapsRecvHex(capsStr);

        parseInfo = L"Capabilities: ";
        parseInfo += caps.substr(0, 80);
        if (caps.length() > 80) parseInfo += L"...";

    } else {
        // Unknown command — can't send via Windows API
        rxHex = L"";
        parseInfo = L"Unknown command — display only (TX computed)";
    }

    std::wostringstream ss;
    ss << L"{\"type\":\"rawResponse\",\"monitor\":" << monitorIndex
       << L",\"txHex\":\"" << txHex << L"\""
       << L",\"rxHex\":\"" << rxHex << L"\""
       << L",\"parsed\":\"" << EscapeJson(parseInfo) << L"\""
       << L"}";
    return ss.str();
}
