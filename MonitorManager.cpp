#include "MonitorManager.h"
#include <physicalmonitorenumerationapi.h>
#include <lowlevelmonitorconfigurationapi.h>
#include <cstdarg>
#include <cstring>
#include <cwctype>

#pragma comment(lib, "dxva2.lib")
#pragma comment(lib, "user32.lib")

MonitorManager::~MonitorManager() {
    DestroyMonitors();
}

void MonitorManager::AddMonitor(HANDLE hPhysicalMonitor, const std::wstring& name) {
    MonitorInfo info;
    info.hPhysicalMonitor = hPhysicalMonitor;
    info.name = name;
    m_monitors.push_back(info);
}

static void WriteLog(const WCHAR* fmt, ...);
static void WriteLog(const std::wstring& msg);

BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC, LPRECT, LPARAM dwData) {
    auto* self = reinterpret_cast<MonitorManager*>(dwData);

    MONITORINFOEXW mi = {sizeof(MONITORINFOEXW)};
    GetMonitorInfoW(hMonitor, &mi);

    DWORD count = 0;
    BOOL hasPhysical = GetNumberOfPhysicalMonitorsFromHMONITOR(hMonitor, &count);

    WCHAR log[512];
    swprintf_s(log, L"Enum callback: monitor=\"%s\" hMon=0x%p physCount=%u hasPhys=%d err=%u",
               mi.szDevice, hMonitor, count, hasPhysical, hasPhysical ? 0 : GetLastError());
    OutputDebugStringW(log);
    self->m_diagLog.push_back(log);

    if (!hasPhysical || count == 0) {
        return TRUE;
    }

    std::vector<PHYSICAL_MONITOR> physicalMonitors(count);
    if (!GetPhysicalMonitorsFromHMONITOR(hMonitor, count, physicalMonitors.data())) {
        swprintf_s(log, L"  GetPhysicalMonitorsFromHMONITOR FAILED, err=%u", GetLastError());
        OutputDebugStringW(log);
        self->m_diagLog.push_back(log);
        return TRUE;
    }

    for (DWORD i = 0; i < count; i++) {
        // Extract model name from DDC/CI capabilities string (e.g. "model(CU34P2C)")
        // This is the only reliable way to get the correct monitor name per handle.
        std::wstring monitorName = physicalMonitors[i].szPhysicalMonitorDescription;

        DWORD capLen = 0;
        if (GetCapabilitiesStringLength(physicalMonitors[i].hPhysicalMonitor, &capLen) && capLen > 0) {
            std::vector<char> capBuf(capLen + 1);
            if (CapabilitiesRequestAndCapabilitiesReply(physicalMonitors[i].hPhysicalMonitor,
                    capBuf.data(), static_cast<DWORD>(capBuf.size()))) {
                capBuf[capLen] = '\0';
                std::string caps(capBuf.data());

                // Parse model(xxx) from capabilities
                const char* modelTag = "model(";
                const char* p = strstr(caps.c_str(), modelTag);
                if (p) {
                    p += strlen(modelTag);
                    const char* end = strchr(p, ')');
                    if (end && end > p) {
                        std::string model(p, end - p);
                        if (!model.empty()) {
                            int wideLen = MultiByteToWideChar(CP_UTF8, 0, model.c_str(), -1, nullptr, 0);
                            if (wideLen > 0) {
                                monitorName.resize(wideLen - 1);
                                MultiByteToWideChar(CP_UTF8, 0, model.c_str(), -1, &monitorName[0], wideLen);
                            }
                        }
                    }
                }
            }
        }

        WCHAR entry[256];
        swprintf_s(entry, L"  Found: \"%s\" (raw: \"%s\") handle=0x%p",
                   monitorName.c_str(),
                   physicalMonitors[i].szPhysicalMonitorDescription,
                   physicalMonitors[i].hPhysicalMonitor);
        OutputDebugStringW(entry);
        WriteLog(entry);
        self->m_diagLog.push_back(entry);

        self->AddMonitor(physicalMonitors[i].hPhysicalMonitor, monitorName);
    }

    return TRUE;
}

static void WriteLog(const std::wstring& msg) {
#ifdef _DEBUG
    WCHAR path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring logPath(path);
    size_t lastSlash = logPath.find_last_of(L"\\/");
    if (lastSlash != std::wstring::npos)
        logPath = logPath.substr(0, lastSlash);
    logPath += L"\\debug.log";

    HANDLE hFile = CreateFileW(logPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ,
                               nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile != INVALID_HANDLE_VALUE) {
        std::string line(msg.begin(), msg.end());
        line += "\r\n";
        DWORD written = 0;
        WriteFile(hFile, line.c_str(), static_cast<DWORD>(line.size()), &written, nullptr);
        CloseHandle(hFile);
    }
#else
    (void)msg;
#endif
}

static void WriteLog(const WCHAR* fmt, ...) {
    WCHAR buf[512];
    va_list args;
    va_start(args, fmt);
    _vsnwprintf_s(buf, _TRUNCATE, fmt, args);
    va_end(args);
    WriteLog(std::wstring(buf));
}

int MonitorManager::EnumerateMonitors() {
    DestroyMonitors();
    m_monitors.clear();
    m_diagLog.clear();

    WriteLog(L"=== EnumerateMonitors start ===");

    if (!EnumDisplayMonitors(nullptr, nullptr, MonitorEnumProc, reinterpret_cast<LPARAM>(this))) {
        WriteLog(L"EnumDisplayMonitors FAILED, GLE=%u", GetLastError());
    }

    WriteLog(L"EnumDisplayMonitors done. physical monitors found: %zu", m_monitors.size());
    for (size_t i = 0; i < m_monitors.size(); i++) {
        WriteLog(L"  [%zu] \"%s\" handle=0x%p", i, m_monitors[i].name.c_str(), m_monitors[i].hPhysicalMonitor);
    }

    return static_cast<int>(m_monitors.size());
}

int MonitorManager::GetMonitorCount() const {
    return static_cast<int>(m_monitors.size());
}

const MonitorInfo* MonitorManager::GetMonitor(int index) const {
    if (index < 0 || index >= static_cast<int>(m_monitors.size()))
        return nullptr;
    return &m_monitors[index];
}

std::wstring MonitorManager::GetCapabilities(int index) {
    auto* mon = GetMonitor(index);
    if (!mon) return L"";

    DWORD len = 0;
    if (!GetCapabilitiesStringLength(mon->hPhysicalMonitor, &len) || len == 0) {
        WriteLog(L"GetCapabilities[%d]: GetCapabilitiesStringLength failed, GLE=%u", index, GetLastError());
        return L"";
    }

    WriteLog(L"GetCapabilities[%d]: expected length=%u", index, len);

    // len from GetCapabilitiesStringLength includes null terminator.
    // The API requires the exact value returned by GetCapabilitiesStringLength.
    std::vector<char> buf(len + 1); // +1 safety margin
    if (!CapabilitiesRequestAndCapabilitiesReply(mon->hPhysicalMonitor, buf.data(), len)) {
        DWORD err = GetLastError();
        WriteLog(L"GetCapabilities[%d]: CapabilitiesRequestAndCapabilitiesReply FAILED, GLE=%u, len=%u",
                 index, err, len);
        return L"";
    }

    buf[len] = '\0';
    WriteLog(L"GetCapabilities[%d]: read %u bytes OK, first 80 chars: %S", index, len, buf.data());

    int wideLen = MultiByteToWideChar(CP_UTF8, 0, buf.data(), -1, nullptr, 0);
    if (wideLen <= 0) {
        WriteLog(L"GetCapabilities[%d]: MultiByteToWideChar sizing failed, GLE=%u", index, GetLastError());
        return L"";
    }
    std::wstring result(wideLen - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, buf.data(), -1, &result[0], wideLen);
    return result;
}

DWORD MonitorManager::GetCapabilitiesStringLen(int index) {
    auto* mon = GetMonitor(index);
    if (!mon) return 0;
    DWORD len = 0;
    if (!GetCapabilitiesStringLength(mon->hPhysicalMonitor, &len))
        return 0;
    return len;
}

std::vector<uint8_t> MonitorManager::GetSupportedVCPCodes(int index) {
    return ParseSupportedVCPCodes(GetCapabilities(index));
}

std::vector<uint8_t> MonitorManager::ParseSupportedVCPCodes(const std::wstring& caps) {
    std::vector<uint8_t> codes;
    if (caps.empty()) return codes;

    // Find "vcp(" in the capabilities string
    size_t pos = caps.find(L"vcp(");
    if (pos == std::wstring::npos) return codes;
    pos += 4; // skip "vcp("

    int depth = 1;
    size_t i = pos;
    while (i < caps.size() && depth > 0) {
        // Skip whitespace
        while (i < caps.size() && std::iswspace(static_cast<wint_t>(caps[i]))) i++;
        if (i >= caps.size()) break;

        if (caps[i] == L'(') {
            depth++;
            i++;
        } else if (caps[i] == L')') {
            depth--;
            i++;
            if (depth == 0) break;
        } else if (std::iswxdigit(static_cast<wint_t>(caps[i]))) {
            // Parse 2-digit hex VCP code
            size_t end = i;
            while (end < caps.size() && std::iswxdigit(static_cast<wint_t>(caps[end]))) end++;
            if (end - i >= 2) {
                std::wstring hexStr = caps.substr(i, 2);
                uint8_t code = static_cast<uint8_t>(std::wcstol(hexStr.c_str(), nullptr, 16));
                codes.push_back(code);
            }
            i = end;
        } else {
            i++; // Skip unknown chars
        }
    }
    return codes;
}

VCPFeature MonitorManager::GetVCPFeature(int index, uint8_t vcpCode) {
    auto* mon = GetMonitor(index);
    VCPFeature result = {0, 0, false};
    if (!mon) return result;

    DWORD current = 0, max = 0;
    if (!::GetVCPFeatureAndVCPFeatureReply(mon->hPhysicalMonitor, vcpCode, nullptr, &current, &max))
        return result;

    result.current = current;
    result.max = max;
    result.valid = true;
    return result;
}

void MonitorManager::SetVCP(int index, uint8_t vcpCode, uint32_t value) {
    auto* mon = GetMonitor(index);
    if (!mon) return;

    ::SetVCPFeature(mon->hPhysicalMonitor, vcpCode, value);
}

void MonitorManager::DestroyMonitors() {
    for (auto& mon : m_monitors) {
        if (mon.hPhysicalMonitor) {
            DestroyPhysicalMonitor(mon.hPhysicalMonitor);
            mon.hPhysicalMonitor = nullptr;
        }
    }
    m_monitors.clear();
}
