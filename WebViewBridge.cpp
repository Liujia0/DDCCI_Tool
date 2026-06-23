#include "WebViewBridge.h"
#include "MonitorManager.h"
#include "SerialPortManager.h"
#include "resource.h"
#include "version.h"
#include <sstream>
#include <algorithm>
#include <cwctype>
#include <cstdarg>
#include <map>
#include <set>
#include <vector>

namespace {

    const wchar_t* GetPreferredI2CDllFileName() {
#ifdef _WIN64
        return L"i2c_dev.dll";
#else
        return L"i2c_dev_ng.dll";
#endif
    }

    int GetPreferredI2CDllResourceId() {
#ifdef _WIN64
        return IDR_I2C_DEV_DLL;
#else
        return IDR_I2C_DEV_NG_DLL;
#endif
    }

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

    std::wstring Utf8ToWideLocal(const std::string& s) {
        if (s.empty()) return std::wstring();
        int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                                      nullptr, 0);
        if (len <= 0) return std::wstring();
        std::wstring out(static_cast<size_t>(len), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                            &out[0], len);
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

    uint8_t ChkXorWithSeed(uint8_t seed, const std::vector<uint8_t>& bytes) {
        uint8_t c = seed;
        for (auto b : bytes) c ^= b;
        return c;
    }

    bool ReplyChecksumMatches(const std::vector<uint8_t>& packet) {
        const uint8_t seeds[] = { 0x6F, 0x50 };
        for (uint8_t seed : seeds) {
            uint8_t checksum = seed;
            for (size_t i = 0; i + 1 < packet.size(); ++i) checksum ^= packet[i];
            if (checksum == packet.back()) return true;
        }
        return false;
    }

    std::wstring BuildVCPGetSendHex(uint8_t vcpCode) {
        std::vector<uint8_t> p = {0x6E, 0x51, 0x82, 0x01, vcpCode};
        p.push_back(ChkXor(p));
        return PacketHex(p);
    }

    std::wstring BuildVCPGetRecvHex(uint8_t vcpCode, uint32_t current, uint32_t max) {
        std::vector<uint8_t> p = {0x6E, 0x88, 0x02, 0x00, vcpCode, 0x00,
            static_cast<uint8_t>((max >> 8) & 0xFF),
            static_cast<uint8_t>(max & 0xFF),
            static_cast<uint8_t>((current >> 8) & 0xFF),
            static_cast<uint8_t>(current & 0xFF)};
        p.push_back(ChkXorWithSeed(0x6F, p));
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
        std::vector<uint8_t> p = {0x6E, 0x81, 0x00};
        p.push_back(ChkXorWithSeed(0x6F, p));
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
        (void)offset;
        std::vector<uint8_t> p = {0x6E};
        uint32_t dataLen = 3 + static_cast<uint32_t>(chunk.size());
        p.push_back(static_cast<uint8_t>(0x80 | (dataLen & 0x7F)));
        p.push_back(0x00); // result: success
        p.push_back(static_cast<uint8_t>((nextOffset >> 8) & 0xFF));
        p.push_back(static_cast<uint8_t>(nextOffset & 0xFF));
        for (char ch : chunk)
            p.push_back(static_cast<uint8_t>(ch));
        p.push_back(ChkXorWithSeed(0x6F, p));
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

    struct ParsedSerialReply {
        bool valid = false;
        std::wstring error;
        std::vector<uint8_t> payload;
    };

    struct RawCommandOutput {
        std::wstring txHex;
        std::wstring rxHex;
        std::wstring parseInfo;
    };

    std::vector<uint8_t> ParseHexStringLocal(const std::wstring& hex) {
        std::vector<uint8_t> bytes;
        const WCHAR* p = hex.c_str();
        while (*p) {
            while (*p == L' ') p++;
            if (!*p) break;

            unsigned int val = 0;
            for (int i = 0; i < 2; ++i) {
                WCHAR ch = *p++;
                if (ch >= L'0' && ch <= L'9') val = (val << 4) | (ch - L'0');
                else if (ch >= L'A' && ch <= L'F') val = (val << 4) | (ch - L'A' + 10);
                else if (ch >= L'a' && ch <= L'f') val = (val << 4) | (ch - L'a' + 10);
                else return bytes;
            }

            bytes.push_back(static_cast<uint8_t>(val));
        }
        return bytes;
    }

    std::wstring BytesToHexStrLocal(const std::vector<uint8_t>& bytes) {
        std::wostringstream ss;
        for (size_t i = 0; i < bytes.size(); ++i) {
            if (i > 0) ss << L' ';
            WCHAR h[4];
            swprintf_s(h, L"%02X", bytes[i]);
            ss << h;
        }
        return ss.str();
    }

    ParsedSerialReply ParseSerialReplyPacket(const std::vector<uint8_t>& packet) {
        ParsedSerialReply out;

        if (packet.size() < 4) {
            out.error = L"Short reply";
            return out;
        }

        if (packet[0] != 0x6E) {
            std::wostringstream ss;
            ss << L"Unexpected target 0x" << std::hex << std::uppercase
               << static_cast<int>(packet[0]);
            out.error = ss.str();
            return out;
        }

        const uint8_t lenByte = packet[1];
        if ((lenByte & 0x80) == 0) {
            out.error = L"Reply length byte missing high bit";
            return out;
        }

        const size_t payloadLen = static_cast<size_t>(lenByte & 0x7F);
        const size_t expectedSize = payloadLen + 3;
        if (packet.size() != expectedSize) {
            std::wostringstream ss;
            ss << L"Reply length mismatch: expected " << expectedSize
               << L" bytes, got " << packet.size();
            out.error = ss.str();
            return out;
        }

        if (!ReplyChecksumMatches(packet)) {
            uint8_t checksumReadSeed = 0x6F;
            for (size_t i = 0; i + 1 < packet.size(); ++i) checksumReadSeed ^= packet[i];
            uint8_t checksumAltSeed = 0x50;
            for (size_t i = 0; i + 1 < packet.size(); ++i) checksumAltSeed ^= packet[i];
            std::wostringstream ss;
            ss << L"Checksum mismatch: calc=0x" << std::hex << std::uppercase
               << static_cast<int>(checksumReadSeed) << L"/0x"
               << static_cast<int>(checksumAltSeed) << L", recv=0x"
               << static_cast<int>(packet.back());
            out.error = ss.str();
            return out;
        }

        out.payload.assign(packet.begin() + 2, packet.end() - 1);
        out.valid = true;
        return out;
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

    bool ContainsNoCaseLocal(const std::wstring& text, const wchar_t* needle) {
        if (!needle || !*needle) return false;

        std::wstring haystack = text;
        std::wstring probe = needle;
        std::transform(haystack.begin(), haystack.end(), haystack.begin(), towlower);
        std::transform(probe.begin(), probe.end(), probe.begin(), towlower);
        return haystack.find(probe) != std::wstring::npos;
    }

    std::wstring NormalizeForMatch(const std::wstring& text) {
        std::wstring out;
        out.reserve(text.size());
        for (wchar_t ch : text) {
            if (std::iswalnum(static_cast<wint_t>(ch))) {
                out.push_back(static_cast<wchar_t>(std::towlower(static_cast<wint_t>(ch))));
            }
        }
        return out;
    }

    bool IsGpuSerialPortName(const std::wstring& portName) {
        return ContainsNoCaseLocal(portName, L"igcl") ||
               ContainsNoCaseLocal(portName, L"intel") ||
               ContainsNoCaseLocal(portName, L"igfxext") ||
               ContainsNoCaseLocal(portName, L"adl") ||
               ContainsNoCaseLocal(portName, L"adlx") ||
               ContainsNoCaseLocal(portName, L"amd") ||
               ContainsNoCaseLocal(portName, L"radeon") ||
               ContainsNoCaseLocal(portName, L"nvapi") ||
               ContainsNoCaseLocal(portName, L"nvidia") ||
               ContainsNoCaseLocal(portName, L"geforce");
    }

    std::wstring ExtractMonitorHintFromSerialPortName(const std::wstring& portName) {
        if (!IsGpuSerialPortName(portName)) {
            return {};
        }

        const size_t closePos = portName.find_last_of(L')');
        if (closePos == std::wstring::npos) {
            return {};
        }

        const size_t openPos = portName.find_last_of(L'(', closePos);
        if (openPos == std::wstring::npos || openPos + 1 >= closePos) {
            return {};
        }

        return portName.substr(openPos + 1, closePos - openPos - 1);
    }

    int FindProxyMonitorIndex(MonitorManager* monitorMgr, const std::wstring& portName) {
        if (!monitorMgr || !IsGpuSerialPortName(portName)) {
            return -1;
        }

        const std::wstring hint = ExtractMonitorHintFromSerialPortName(portName);
        const std::wstring normalizedHint = NormalizeForMatch(hint);
        const std::wstring normalizedPort = NormalizeForMatch(portName);

        for (int index = 0; index < monitorMgr->GetMonitorCount(); ++index) {
            const MonitorInfo* mon = monitorMgr->GetMonitor(index);
            if (!mon) {
                continue;
            }

            const std::wstring normalizedMonitorName = NormalizeForMatch(mon->name);
            if (normalizedMonitorName.empty()) {
                continue;
            }

            if (!normalizedHint.empty() && normalizedMonitorName == normalizedHint) {
                return index;
            }

            if (!normalizedHint.empty() &&
                (normalizedMonitorName.find(normalizedHint) != std::wstring::npos ||
                 normalizedHint.find(normalizedMonitorName) != std::wstring::npos)) {
                return index;
            }

            if (normalizedPort.find(normalizedMonitorName) != std::wstring::npos) {
                return index;
            }
        }

        return -1;
    }

    struct SerialPortAssignments {
        std::map<int, SerialPortInfo> monitorRawPorts;
        std::vector<SerialPortInfo> unmergedPorts;
    };

    SerialPortAssignments BuildSerialPortAssignments(MonitorManager* monitorMgr,
                                                     SerialPortManager* serialMgr) {
        SerialPortAssignments assignments;
        if (!serialMgr) {
            return assignments;
        }

        const auto ports = serialMgr->EnumeratePorts();
        std::set<int> matchedMonitors;
        for (const auto& port : ports) {
            const std::wstring matchText = !port.description.empty() ? port.description : port.portName;
            const bool isGpuPort = IsGpuSerialPortName(matchText) || IsGpuSerialPortName(port.portName);
            const int proxyMonitorIndex = isGpuPort ? FindProxyMonitorIndex(monitorMgr, matchText) : -1;

            if (proxyMonitorIndex >= 0 && matchedMonitors.insert(proxyMonitorIndex).second) {
                assignments.monitorRawPorts.emplace(proxyMonitorIndex, port);
                continue;
            }

            assignments.unmergedPorts.push_back(port);
        }

        return assignments;
    }

    bool BuildMonitorRawCommandOutput(MonitorManager* monitorMgr,
                                      int monitorIndex,
                                      const std::wstring& bodyHex,
                                      RawCommandOutput& output,
                                      std::wstring& error) {
        output = {};
        error.clear();

        if (!monitorMgr || !monitorMgr->GetMonitor(monitorIndex)) {
            error = L"Monitor not found";
            return false;
        }

        std::vector<uint8_t> body = ParseHexStringLocal(bodyHex);
        if (body.empty()) {
            error = L"Invalid hex body";
            return false;
        }

        std::vector<uint8_t> tx = {0x6E, 0x51};
        tx.push_back(static_cast<uint8_t>(0x80 | (body.size() & 0x7F)));
        for (auto b : body) tx.push_back(b);
        tx.push_back(ChkXor(tx));

        output.txHex = BytesToHexStrLocal(tx);

        if (body.size() >= 2 && body[0] == 0x01) {
            const uint8_t vcp = body[1];
            const auto feat = monitorMgr->GetVCPFeature(monitorIndex, vcp);
            output.rxHex = BuildVCPGetRecvHex(vcp, feat.current, feat.max);

            std::wostringstream info;
            info << L"GetVCP " << std::hex << std::uppercase << static_cast<int>(vcp)
                 << L": current=" << std::dec << feat.current
                 << L", max=" << feat.max;
            if (!feat.valid) {
                info << L" (fallback)";
            }
            output.parseInfo = info.str();
            return true;
        }

        if (body.size() >= 4 && body[0] == 0x03) {
            const uint8_t vcp = body[1];
            const uint32_t value = (static_cast<uint32_t>(body[2]) << 8) | body[3];
            monitorMgr->SetVCP(monitorIndex, vcp, value);
            output.rxHex = BuildVCPSetRecvHex();

            std::wostringstream info;
            info << L"SetVCP " << std::hex << std::uppercase << static_cast<int>(vcp)
                 << L" = " << std::dec << value
                 << L" (fallback)";
            output.parseInfo = info.str();
            return true;
        }

        if (body.size() >= 3 && body[0] == 0xF3) {
            const auto caps = monitorMgr->GetCapabilities(monitorIndex);
            const std::string capsStr = WideToUtf8(caps);
            output.rxHex = BuildCapsRecvHex(capsStr);

            output.parseInfo = L"Capabilities (fallback): ";
            output.parseInfo += caps.substr(0, 80);
            if (caps.length() > 80) output.parseInfo += L"...";
            return true;
        }

        output.parseInfo = L"Unknown command - display only (DXVA2 fallback)";
        return true;
    }

}

WebViewBridge::WebViewBridge(MonitorManager* monitorMgr, SerialPortManager* serialMgr)
    : m_monitorMgr(monitorMgr), m_serialMgr(serialMgr) {}

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
    // If web resources have been extracted to temp, use that path
    if (!m_webDir.empty())
        return m_webDir;

    // Fallback: look for web/ folder next to the exe (Debug/dev mode)
    WCHAR path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring dir(path);
    size_t lastSlash = dir.find_last_of(L"\\/");
    if (lastSlash != std::wstring::npos)
        dir = dir.substr(0, lastSlash);
    return dir + L"\\web";
}

// ---- Embedded web resource extraction ----

namespace {

    // Resource entry: { resource ID, filename to write }
    struct WebResourceEntry {
        int resourceId;
        const wchar_t* fileName;
    };

    bool SaveResourceToFile(const std::wstring& dirPath, int resourceId,
                            const wchar_t* fileName) {
        HRSRC hRes = FindResourceW(nullptr, MAKEINTRESOURCEW(resourceId), L"WEBRES");
        if (!hRes) return false;

        HGLOBAL hData = LoadResource(nullptr, hRes);
        if (!hData) return false;

        const void* pData = LockResource(hData);
        DWORD size = SizeofResource(nullptr, hRes);
        if (!pData || size == 0) return false;

        std::wstring filePath = dirPath + L"\\" + fileName;
        HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_WRITE, 0, nullptr,
                                   CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile == INVALID_HANDLE_VALUE) return false;

        DWORD written = 0;
        BOOL ok = WriteFile(hFile, pData, size, &written, nullptr);
        CloseHandle(hFile);
        return ok && written == size;
    }

} // anonymous namespace

void WebViewBridge::ExtractWebResources(HINSTANCE hInstance) {
    (void)hInstance; // resources are in the current module
    const std::wstring preferredDllName = GetPreferredI2CDllFileName();
    const int preferredDllResourceId = GetPreferredI2CDllResourceId();

    // Check if web/ exists next to the exe (dev mode with copied files)
    std::wstring devWebDir = []() {
        WCHAR path[MAX_PATH];
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        std::wstring dir(path);
        size_t lastSlash = dir.find_last_of(L"\\/");
        if (lastSlash != std::wstring::npos)
            dir = dir.substr(0, lastSlash);
        return dir + L"\\web";
    }();

    const std::wstring devBaseDir = devWebDir.substr(0, devWebDir.size() - 4);
    DWORD devAttr = GetFileAttributesW((devWebDir + L"\\index.html").c_str());
    const bool hasDevWeb = (devAttr != INVALID_FILE_ATTRIBUTES);
    if (hasDevWeb) {
        m_webDir = devWebDir;
        m_dataDir = devBaseDir + L"WV2Data";
        m_dllDir = devBaseDir;
    }

    const std::wstring devDllPath = devBaseDir + L"\\" + preferredDllName;
    const DWORD devDllAttr = GetFileAttributesW(devDllPath.c_str());
    if (hasDevWeb && devDllAttr != INVALID_FILE_ATTRIBUTES) {
        // web/ folder exists alongside exe — use it directly (Debug/dev mode)
        if (m_serialMgr) {
            m_serialMgr->LoadI2CDev(devDllPath);
        }
        return;
    }

    // Extract embedded resources to %LOCALAPPDATA%\DDCCI_Tool\web (cache dir)
    WCHAR localAppData[MAX_PATH] = {};
    DWORD envLen = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH);
    if (envLen == 0 || envLen >= MAX_PATH) {
        // Fallback to TEMP — strip trailing backslash for consistent path building
        DWORD len = GetTempPathW(MAX_PATH, localAppData);
        if (len > 1 && localAppData[len - 1] == L'\\')
            localAppData[len - 1] = L'\0';
    }

    std::wstring baseDir = std::wstring(localAppData) + L"\\DDCCI_Tool";
    std::wstring extractDir = baseDir + L"\\web";

    // Create directory (ignore errors if it already exists)
    CreateDirectoryW(baseDir.c_str(), nullptr);
    CreateDirectoryW(extractDir.c_str(), nullptr);
    if (!hasDevWeb) {
        m_dataDir = baseDir + L"\\WV2Data";
    }

    static const WebResourceEntry entries[] = {
        { IDR_WEB_INDEX_HTML, L"index.html" },
        { IDR_WEB_STYLE_CSS,  L"style.css"  },
        { IDR_WEB_APP_JS,     L"app.js"     },
        { IDR_WEB_MCCS_JS,    L"mccs.js"    },
        { IDR_WEB_LOGO_SVG,   L"logo.svg"   },
    };

    for (const auto& entry : entries) {
        std::wstring filePath = extractDir + L"\\" + entry.fileName;

        // Skip if file already exists and matches resource size
        HANDLE hExisting = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                       nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hExisting != INVALID_HANDLE_VALUE) {
            LARGE_INTEGER fileSize;
            if (GetFileSizeEx(hExisting, &fileSize)) {
                HRSRC hRes = FindResourceW(nullptr, MAKEINTRESOURCEW(entry.resourceId), L"WEBRES");
                if (hRes && static_cast<DWORD>(fileSize.QuadPart) == SizeofResource(nullptr, hRes)) {
                    CloseHandle(hExisting);
                    continue; // file already up-to-date
                }
            }
            CloseHandle(hExisting);
        }

        SaveResourceToFile(extractDir, entry.resourceId, entry.fileName);
    }

    // Extract transport DLLs to baseDir (%LOCALAPPDATA%\DDCCI_Tool\)
    m_dllDir = baseDir;
    const struct {
        int resourceId;
        const wchar_t* fileName;
    } dllEntries[] = {
        { preferredDllResourceId, preferredDllName.c_str() },
    };

    for (const auto& dllEntry : dllEntries) {
        std::wstring dllFilePath = baseDir + L"\\" + dllEntry.fileName;
        bool needExtract = true;

        HANDLE hExisting = CreateFileW(dllFilePath.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                       nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hExisting != INVALID_HANDLE_VALUE) {
            LARGE_INTEGER fileSize;
            if (GetFileSizeEx(hExisting, &fileSize)) {
                HRSRC hRes = FindResourceW(nullptr, MAKEINTRESOURCEW(dllEntry.resourceId), L"WEBRES");
                if (hRes && static_cast<DWORD>(fileSize.QuadPart) == SizeofResource(nullptr, hRes)) {
                    needExtract = false;
                }
            }
            CloseHandle(hExisting);
        }

        if (needExtract) {
            SaveResourceToFile(baseDir, dllEntry.resourceId, dllEntry.fileName);
        }
    }

    if (m_serialMgr) {
        m_serialMgr->LoadI2CDev(baseDir + L"\\" + preferredDllName);
    }

    if (!hasDevWeb) {
        m_webDir = extractDir;
    }
}

HRESULT WebViewBridge::Initialize(HWND hwnd) {
    m_hwnd = hwnd;

    return CreateCoreWebView2EnvironmentWithOptions(
        nullptr, m_dataDir.c_str(), nullptr,
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
                              L"Web UI resources could not be loaded.\n"
                              L"Please reinstall the application.",
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

    // Tag update-related responses with "_update":true so that JS routes them
    // to handleUpdateResponse() instead of the normal dispatchResponse() path.
    std::wstring method = ExtractJsonString(rawMessage, L"method");
    bool isUpdateMethod = (method == L"startUpdateCheck" || method == L"pollUpdateCheck" ||
                           method == L"downloadUpdate" || method == L"getDownloadProgress" ||
                           method == L"applyUpdate" || method == L"openUrl");
    if (isUpdateMethod && !response.empty() && response[0] == L'{') {
        response = L"{\"_update\":true," + response.substr(1);
    }

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
        // Re-scan physical monitors so hot-plug/unplug is reflected. Without
        // this, BuildMonitorList() would only return the cached list captured
        // at startup and the Refresh button would appear to do nothing.
        m_monitorMgr->EnumerateMonitors();
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

    if (method == L"startUpdateCheck") return HandleStartUpdateCheck();
    if (method == L"pollUpdateCheck")  return HandlePollUpdateCheck();
    if (method == L"downloadUpdate")   return HandleDownloadUpdate(json);
    if (method == L"getDownloadProgress") return HandleGetDownloadProgress();
    if (method == L"applyUpdate")      return HandleApplyUpdate();
    if (method == L"openUrl")          return HandleOpenUrl(json);
    if (method == L"getVersion")       return L"{\"type\":\"appVersion\",\"version\":\"" VERSION_WDOT L"\"}";

    // Serial port methods
    if (method == L"enumerateSerialPorts") return BuildSerialPortList();
    if (method == L"openSerialPort")       return HandleOpenSerialPort(json);
    if (method == L"closeSerialPort")      return HandleCloseSerialPort(json);
    if (method == L"sendSerialRaw")        return HandleSendSerialRaw(json);

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
    const SerialPortAssignments assignments = BuildSerialPortAssignments(m_monitorMgr, m_serialMgr);
    std::wostringstream ss;
    ss << L"{\"type\":\"monitorList\",\"monitors\":[";
    int count = m_monitorMgr->GetMonitorCount();
    for (int i = 0; i < count; i++) {
        if (i > 0) ss << L",";
        auto* mon = m_monitorMgr->GetMonitor(i);
        const auto rawPortIt = assignments.monitorRawPorts.find(i);
        ss << L"{\"index\":" << i
           << L",\"name\":\"" << EscapeJson(mon ? mon->name : L"Unknown") << L"\"";
        if (rawPortIt != assignments.monitorRawPorts.end()) {
            ss << L",\"rawPortName\":\"" << EscapeJson(rawPortIt->second.portName) << L"\""
               << L",\"rawPortLabel\":\"" << EscapeJson(rawPortIt->second.description) << L"\"";
        }
        ss << L"}";
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
    RawCommandOutput output;
    std::wstring error;
    if (!BuildMonitorRawCommandOutput(m_monitorMgr, monitorIndex, bodyHex, output, error)) {
        return BuildError(error);
    }

    std::wostringstream ss;
    ss << L"{\"type\":\"rawResponse\",\"monitor\":" << monitorIndex
       << L",\"txHex\":\"" << output.txHex << L"\""
       << L",\"rxHex\":\"" << output.rxHex << L"\""
       << L",\"parsed\":\"" << EscapeJson(output.parseInfo) << L"\""
       << L"}";
    return ss.str();
}

// ---- Serial port methods ----

std::wstring WebViewBridge::BuildSerialPortList() {
    const SerialPortAssignments assignments = BuildSerialPortAssignments(m_monitorMgr, m_serialMgr);
    std::wostringstream ss;
    ss << L"{\"type\":\"serialPortList\",\"serialPorts\":[";

    for (size_t i = 0; i < assignments.unmergedPorts.size(); i++) {
        if (i > 0) ss << L",";
        ss << L"{\"index\":" << (100 + i)
           << L",\"name\":\"" << EscapeJson(assignments.unmergedPorts[i].description) << L"\""
           << L",\"isSerial\":true"
           << L",\"portName\":\"" << EscapeJson(assignments.unmergedPorts[i].portName) << L"\""
           << L"}";
    }

    ss << L"]}";
    return ss.str();
}

std::wstring WebViewBridge::HandleOpenSerialPort(const std::wstring& json) {
    std::wstring portName = ExtractJsonString(json, L"portName");
    if (portName.empty()) return BuildError(L"Missing portName");

    BridgeLog(L"HandleOpenSerialPort: request port=%s", portName.c_str());
    const int proxyMonitorIndex = FindProxyMonitorIndex(m_monitorMgr, portName);
    if (proxyMonitorIndex >= 0) {
        BridgeLog(L"HandleOpenSerialPort: using DXVA2 fallback proxy monitor=%d for port=%s",
                  proxyMonitorIndex, portName.c_str());
        return L"{\"type\":\"serialPortOpened\",\"success\":true,\"portName\":\""
               + EscapeJson(portName) + L"\"}";
    }

    if (!m_serialMgr) return BuildError(L"Serial port manager not available");

    if (m_serialMgr->OpenPort(portName)) {
        BridgeLog(L"HandleOpenSerialPort: opened port=%s", portName.c_str());
        return L"{\"type\":\"serialPortOpened\",\"success\":true,\"portName\":\"" 
               + EscapeJson(portName) + L"\"}";
    } else {
        BridgeLog(L"HandleOpenSerialPort: failed port=%s", portName.c_str());
        return L"{\"type\":\"serialPortOpened\",\"success\":false,\"portName\":\"" 
               + EscapeJson(portName) + L"\",\"error\":\"Failed to open port\"}";
    }
}

std::wstring WebViewBridge::HandleCloseSerialPort(const std::wstring&) {
    if (m_serialMgr) {
        m_serialMgr->ClosePort();
    }
    return L"{\"type\":\"serialPortClosed\",\"success\":true}";
}

std::wstring WebViewBridge::HandleSendSerialRaw(const std::wstring& json) {
    std::wstring portName = ExtractJsonString(json, L"portName");
    std::wstring bodyHex = ExtractJsonString(json, L"bodyHex");
    int readWaitMs = ExtractJsonInt(json, L"readWaitMs");

    if (portName.empty() || bodyHex.empty()) return BuildError(L"Missing parameters");
    if (readWaitMs < 0) {
        readWaitMs = static_cast<int>(SerialPortManager::DEFAULT_RAW_READ_WAIT_MS);
    }

    std::vector<uint8_t> body = ParseHexString(bodyHex);
    if (body.empty()) return BuildError(L"Invalid hex body");

    BridgeLog(L"HandleSendSerialRaw: port=%s body=%s readWaitMs=%d",
              portName.c_str(), bodyHex.c_str(), readWaitMs);
    const int proxyMonitorIndex = FindProxyMonitorIndex(m_monitorMgr, portName);
    if (proxyMonitorIndex >= 0) {
        BridgeLog(L"HandleSendSerialRaw: proxy monitor=%d port=%s uses native serial RAW transport",
                  proxyMonitorIndex, portName.c_str());
    }

    if (!m_serialMgr) return BuildError(L"Serial port manager not available");
    m_serialMgr->SetRawReadWaitMs(static_cast<DWORD>(readWaitMs));

    // Ensure port is open
    if (!m_serialMgr->IsOpen() || m_serialMgr->GetOpenPortName() != portName) {
        if (!m_serialMgr->OpenPort(portName)) {
            BridgeLog(L"HandleSendSerialRaw: open failed port=%s", portName.c_str());
            return BuildError(L"Failed to open serial port: " + portName);
        }
    }

    // Build TX hex for display
    std::vector<uint8_t> tx = {0x6E, 0x51};
    tx.push_back(static_cast<uint8_t>(0x80 | (body.size() & 0x7F)));
    for (auto b : body) tx.push_back(b);
    tx.push_back(ChkXor(tx));
    std::wstring txHex = BytesToHexStr(tx);

    // Send via serial port I2C
    std::vector<uint8_t> rxData;
    std::string error;
    bool ok = m_serialMgr->DDCSendRaw(body, rxData, error);
    BridgeLog(L"HandleSendSerialRaw: send result=%d rxLen=%zu err=%S",
              ok ? 1 : 0, rxData.size(), error.c_str());

    std::wstring rxHex, parseInfo;
    if (ok && !rxData.empty()) {
        rxHex = BytesToHexStr(rxData);
        ParsedSerialReply reply = ParseSerialReplyPacket(rxData);

        // Try to parse the response
        if (body.size() >= 2 && body[0] == 0x01) {
            // GetVCP response
            if (reply.valid && reply.payload.size() >= 8 && reply.payload[0] == 0x02) {
                uint8_t vcp = body[1];
                uint32_t maxVal = (static_cast<uint32_t>(reply.payload[4]) << 8) | reply.payload[5];
                uint32_t curVal = (static_cast<uint32_t>(reply.payload[6]) << 8) | reply.payload[7];
                uint8_t resultCode = reply.payload[1];
                uint8_t replyVcp = reply.payload[2];
                uint8_t typeCode = reply.payload[3];
                std::wostringstream info;
                info << L"GetVCP " << std::hex << std::uppercase << static_cast<int>(vcp)
                     << L": current=" << std::dec << curVal
                     << L", max=" << maxVal
                     << L", result=0x" << std::hex << std::uppercase << static_cast<int>(resultCode)
                     << L", replyVcp=0x" << static_cast<int>(replyVcp)
                     << L", type=0x" << static_cast<int>(typeCode);
                parseInfo = info.str();
            } else if (reply.valid) {
                parseInfo = L"GetVCP: unexpected payload (" + std::to_wstring(reply.payload.size()) + L" bytes)";
            } else if (!reply.error.empty()) {
                parseInfo = L"GetVCP: invalid reply - " + reply.error;
            } else {
                parseInfo = L"GetVCP: short response (" + std::to_wstring(rxData.size()) + L" bytes)";
            }
        } else if (body.size() >= 4 && body[0] == 0x03) {
            // SetVCP response
            uint8_t vcp = body[1];
            uint32_t value = (static_cast<uint32_t>(body[2]) << 8) | body[3];
            std::wostringstream info;
            info << L"SetVCP " << std::hex << std::uppercase << static_cast<int>(vcp)
                 << L" = " << std::dec << value;
            if (reply.valid && !reply.payload.empty()) {
                info << L", result=0x" << std::hex << std::uppercase
                     << static_cast<int>(reply.payload[0]);
            } else if (!reply.error.empty()) {
                info << L", invalid reply: " << reply.error;
            }
            parseInfo = info.str();
        } else if (reply.valid && body.size() >= 1 && body[0] == 0xF3 && reply.payload.size() >= 3) {
            uint8_t resultCode = reply.payload[0];
            uint16_t nextOffset = (static_cast<uint16_t>(reply.payload[1]) << 8) | reply.payload[2];
            size_t chunkLen = reply.payload.size() - 3;
            std::wostringstream info;
            info << L"Capabilities chunk: result=0x" << std::hex << std::uppercase
                 << static_cast<int>(resultCode)
                 << L", nextOffset=" << std::dec << nextOffset
                 << L", dataBytes=" << chunkLen;
            parseInfo = info.str();
        } else if (reply.valid) {
            parseInfo = L"Response: " + std::to_wstring(reply.payload.size()) + L" payload bytes";
        } else if (!reply.error.empty()) {
            parseInfo = L"Invalid reply: " + reply.error;
        } else {
            parseInfo = L"Response: " + std::to_wstring(rxData.size()) + L" bytes";
        }
    } else if (ok) {
        rxHex = L"";
        parseInfo = L"Write sent successfully (no reply)";
    } else {
        rxHex = L"";
        std::wstring errW(error.begin(), error.end());
        parseInfo = L"Error: " + errW;
    }

    std::wostringstream ss;
    ss << L"{\"type\":\"serialRawResponse\""
       << L",\"txHex\":\"" << txHex << L"\""
       << L",\"rxHex\":\"" << rxHex << L"\""
       << L",\"parsed\":\"" << EscapeJson(parseInfo) << L"\""
       << L"}";
    std::wstring response = ss.str();
    BridgeLog(L"HandleSendSerialRaw: response tx=%s rx=%s parsed=%s",
              txHex.c_str(), rxHex.c_str(), parseInfo.c_str());
    return response;
}

// ---- Update check handlers ----

std::wstring WebViewBridge::HandleStartUpdateCheck() {
    std::lock_guard<std::mutex> lock(m_checkMutex);
    BridgeLog(L"HandleStartUpdateCheck: begin");

    // If already in progress and future not yet ready, just return success
    if (m_checkInProgress && m_checkFuture.valid() &&
        m_checkFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        BridgeLog(L"HandleStartUpdateCheck: already pending");
        return L"{\"success\":true}";
    }

    try {
        m_checkFuture = std::async(std::launch::async, []() {
            return UpdateChecker::CheckForUpdate();
        });
        m_checkInProgress = true;
        BridgeLog(L"HandleStartUpdateCheck: async launched");
        return L"{\"success\":true}";
    } catch (const std::exception& ex) {
        std::wstring err = Utf8ToWideLocal(ex.what());
        BridgeLog(L"HandleStartUpdateCheck: std::exception=%s", err.c_str());
        return BuildError(L"startUpdateCheck failed: " + err);
    } catch (...) {
        BridgeLog(L"HandleStartUpdateCheck: unknown exception");
        return BuildError(L"startUpdateCheck failed");
    }
}

std::wstring WebViewBridge::HandlePollUpdateCheck() {
    std::lock_guard<std::mutex> lock(m_checkMutex);
    BridgeLog(L"HandlePollUpdateCheck: begin inProgress=%d valid=%d",
              m_checkInProgress ? 1 : 0, m_checkFuture.valid() ? 1 : 0);

    if (!m_checkInProgress || !m_checkFuture.valid()) {
        return L"{\"success\":true,\"pending\":false,\"hasUpdate\":false}";
    }

    if (m_checkFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return L"{\"success\":true,\"pending\":true}";
    }

    // Future is ready — get result
    UpdateInfo info = m_checkFuture.get();
    m_checkInProgress = false;

    std::wostringstream ss;
    ss << L"{"
       << L"\"success\":" << (info.error.empty() ? L"true" : L"false") << L","
       << L"\"pending\":false,"
       << L"\"hasUpdate\":" << (info.hasUpdate ? L"true" : L"false") << L","
       << L"\"currentVersion\":\"" << EscapeJson(info.currentVersion) << L"\","
       << L"\"latestVersion\":\"" << EscapeJson(info.latestVersion) << L"\","
       << L"\"releaseUrl\":\"" << EscapeJson(info.releaseUrl) << L"\","
       << L"\"downloadUrl\":\"" << EscapeJson(info.downloadUrl) << L"\","
       << L"\"assetName\":\"" << EscapeJson(info.assetName) << L"\","
       << L"\"assetSize\":" << info.assetSize << L","
       << L"\"releaseNotes\":\"" << EscapeJson(info.releaseNotes) << L"\","
       << L"\"error\":\"" << EscapeJson(info.error) << L"\""
       << L"}";
    return ss.str();
}

std::wstring WebViewBridge::HandleDownloadUpdate(const std::wstring& json) {
    std::wstring url = ExtractJsonString(json, L"url");
    if (url.empty()) return BuildError(L"Missing download URL");

    {
        std::lock_guard<std::mutex> lock(m_downloadMutex);
        m_downloadProgress = 0.0;
        m_pendingUpdatePath.clear();
    }

    m_downloadFuture = std::async(std::launch::async, [this, url]() {
        return UpdateChecker::DownloadUpdate(url, [this](double progress) {
            std::lock_guard<std::mutex> lock(m_downloadMutex);
            m_downloadProgress = progress;
        });
    });

    return L"{\"success\":true,\"started\":true}";
}

std::wstring WebViewBridge::HandleGetDownloadProgress() {
    std::lock_guard<std::mutex> lock(m_downloadMutex);

    double progress = m_downloadProgress;
    bool done = false;
    std::wstring error;
    std::wstring path;

    if (m_downloadFuture.valid() &&
        m_downloadFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        done = true;
        path = m_downloadFuture.get();
        if (path.empty()) {
            error = L"Download failed";
        } else {
            m_pendingUpdatePath = path;
        }
    }

    bool downloadOk = !done || error.empty(); // success=false only when done AND failed
    std::wostringstream ss;
    ss << L"{"
       << L"\"success\":" << (downloadOk ? L"true" : L"false") << L","
       << L"\"progress\":" << progress << L","
       << L"\"done\":" << (done ? L"true" : L"false") << L","
       << L"\"error\":\"" << EscapeJson(error) << L"\","
       << L"\"path\":\"" << EscapeJson(path) << L"\""
       << L"}";
    return ss.str();
}

std::wstring WebViewBridge::HandleApplyUpdate() {
    std::wstring updatePath;
    {
        std::lock_guard<std::mutex> lock(m_downloadMutex);
        updatePath = m_pendingUpdatePath;
    }

    if (updatePath.empty()) {
        return BuildError(L"No pending update");
    }

    if (!UpdateChecker::ApplyAndRestart(updatePath)) {
        return BuildError(L"Failed to prepare update");
    }

    // Post WM_CLOSE to allow JS to receive response before exit
    if (m_hwnd) {
        PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
    }

    return L"{\"success\":true}";
}

std::wstring WebViewBridge::HandleOpenUrl(const std::wstring& json) {
    std::wstring url = ExtractJsonString(json, L"url");
    if (url.empty()) return BuildError(L"Missing URL");

    UpdateChecker::OpenUrl(url);
    return L"{\"success\":true}";
}
