#include "MonitorManager.h"
#include <physicalmonitorenumerationapi.h>
#include <lowlevelmonitorconfigurationapi.h>
#include <SetupAPI.h>
#include <devguid.h>
#include <regstr.h>
#include <cstdarg>
#include <cstring>
#include <cwctype>
#include <map>

#pragma comment(lib, "dxva2.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "setupapi.lib")

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

// ---- EDID-based monitor name retrieval (aligned with Windows Display Settings) ----

static std::wstring ParseEdidMonitorName(const BYTE* edid, DWORD size) {
    // EDID must be at least 128 bytes; check header signature
    if (size < 128) return L"";
    static const BYTE header[] = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};
    if (memcmp(edid, header, 8) != 0) return L"";

    // 4 descriptor blocks at bytes 54-71, 72-89, 90-107, 108-125 (18 bytes each)
    for (int i = 0; i < 4; i++) {
        const BYTE* desc = edid + 54 + i * 18;
        // Monitor name descriptor: bytes 0-1 = 0x0000, byte 3 = 0xFC
        if (desc[0] == 0x00 && desc[1] == 0x00 && desc[3] == 0xFC) {
            char name[14];
            memcpy(name, desc + 5, 13);
            name[13] = '\0';
            // Strip trailing newline (0x0A) and spaces
            for (int j = 12; j >= 0; j--) {
                if (name[j] == '\n' || name[j] == ' ')
                    name[j] = '\0';
                else
                    break;
            }
            if (name[0] != '\0') {
                int wideLen = MultiByteToWideChar(CP_UTF8, 0, name, -1, nullptr, 0);
                if (wideLen > 0) {
                    std::wstring result(wideLen - 1, L'\0');
                    MultiByteToWideChar(CP_UTF8, 0, name, -1, &result[0], wideLen);
                    return result;
                }
            }
        }
    }
    return L"";
}

// Extract device instance ID from device interface path.
// Input:  "\\?\DISPLAY#VSCD748#4&35989c15&1&UID4145#{e6f07b5f-...}"
// Output: "DISPLAY\VSCD748\4&35989c15&1&UID4145"
static std::wstring ExtractInstanceIdFromDevPath(const std::wstring& devPath) {
    // Skip "\\?\" prefix
    size_t start = 0;
    if (devPath.size() >= 4 && devPath[0] == L'\\' && devPath[1] == L'\\'
        && devPath[2] == L'?' && devPath[3] == L'\\')
        start = 4;

    // Find "#{guid}" suffix
    size_t end = devPath.find(L"#{", start);
    if (end == std::wstring::npos)
        end = devPath.size();

    // Extract middle part and convert '#' → '\'
    std::wstring instanceId = devPath.substr(start, end - start);
    for (auto& ch : instanceId) {
        if (ch == L'#') ch = L'\\';
    }
    return instanceId;
}

static std::wstring GetMonitorDevicePath(const WCHAR* adapterDevice, DWORD childIndex) {
    DISPLAY_DEVICEW monitorDev = {};
    monitorDev.cb = sizeof(monitorDev);
    if (!EnumDisplayDevicesW(adapterDevice, childIndex, &monitorDev, EDD_GET_DEVICE_INTERFACE_NAME))
        return L"";
    return std::wstring(monitorDev.DeviceID);
}

// Build map: device-instance-ID (uppercase) → EDID monitor name
static std::map<std::wstring, std::wstring> BuildEdidNameMap() {
    std::map<std::wstring, std::wstring> edidMap;

    HDEVINFO devInfo = SetupDiGetClassDevsW(
        &GUID_DEVCLASS_MONITOR, nullptr, nullptr, DIGCF_PRESENT);
    if (devInfo == INVALID_HANDLE_VALUE) {
        WriteLog(L"BuildEdidNameMap: SetupDiGetClassDevs FAILED, err=%u", GetLastError());
        return edidMap;
    }

    SP_DEVINFO_DATA devData = {};
    devData.cbSize = sizeof(SP_DEVINFO_DATA);
    DWORD totalDevices = 0;

    for (DWORD i = 0; SetupDiEnumDeviceInfo(devInfo, i, &devData); i++) {
        totalDevices++;

        WCHAR instanceId[512] = {};
        if (!SetupDiGetDeviceInstanceIdW(devInfo, &devData, instanceId,
                                          _countof(instanceId), nullptr)) {
            WriteLog(L"  [%u] SetupDiGetDeviceInstanceId FAILED, err=%u", i, GetLastError());
            continue;
        }

        // Read EDID blob from device registry
        HKEY hKey = SetupDiOpenDevRegKey(devInfo, &devData,
            DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
        if (hKey == INVALID_HANDLE_VALUE) {
            WriteLog(L"  [%u] \"%s\" RegKey FAILED, err=%u", i, instanceId, GetLastError());
            continue;
        }

        BYTE edidData[512] = {};
        DWORD edidSize = sizeof(edidData);
        DWORD type = 0;
        LONG regResult = RegQueryValueExW(hKey, L"EDID", nullptr, &type, edidData, &edidSize);
        RegCloseKey(hKey);

        if (regResult != ERROR_SUCCESS) {
            WriteLog(L"  [%u] \"%s\" EDID read FAILED, err=%ld", i, instanceId, regResult);
            continue;
        }
        if (type != REG_BINARY || edidSize < 128) {
            WriteLog(L"  [%u] \"%s\" EDID invalid: type=%u size=%u", i, instanceId, type, edidSize);
            continue;
        }

        std::wstring name = ParseEdidMonitorName(edidData, edidSize);
        if (name.empty()) {
            WriteLog(L"  [%u] \"%s\" EDID parse: no monitor name descriptor found", i, instanceId);
            continue;
        }

        // Store uppercase instance ID → name for case-insensitive matching
        std::wstring key(instanceId);
        for (auto& ch : key) ch = towupper(ch);
        edidMap[key] = name;

        WriteLog(L"  [%u] OK: name=\"%s\" instance=\"%s\"", i, name.c_str(), instanceId);
    }

    WriteLog(L"BuildEdidNameMap: %u devices enumerated, %zu names mapped", totalDevices, edidMap.size());
    SetupDiDestroyDeviceInfoList(devInfo);
    return edidMap;
}

// ---- Monitor enumeration callback ----

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
        // Look up monitor name from EDID (aligned with Windows Display Settings)
        std::wstring monitorName = physicalMonitors[i].szPhysicalMonitorDescription;

        std::wstring devPath = GetMonitorDevicePath(mi.szDevice, i);
        std::wstring instanceId = ExtractInstanceIdFromDevPath(devPath);
        // Uppercase for case-insensitive matching (SetupAPI returns uppercase)
        for (auto& ch : instanceId) ch = towupper(ch);

        WriteLog(L"  [%u] instanceId=\"%s\" (adapter=\"%s\")", i, instanceId.c_str(), mi.szDevice);

        if (!instanceId.empty()) {
            auto it = self->m_edidNameMap.find(instanceId);
            if (it != self->m_edidNameMap.end()) {
                monitorName = it->second;
                WriteLog(L"  [%u] EDID match -> \"%s\"", i, monitorName.c_str());
            } else {
                WriteLog(L"  [%u] EDID NO MATCH for instanceId", i);
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
        std::string line;
        if (!msg.empty()) {
            int len = WideCharToMultiByte(CP_UTF8, 0, msg.c_str(), static_cast<int>(msg.size()),
                                          nullptr, 0, nullptr, nullptr);
            if (len > 0) {
                line.resize(len);
                WideCharToMultiByte(CP_UTF8, 0, msg.c_str(), static_cast<int>(msg.size()),
                                    &line[0], len, nullptr, nullptr);
            }
        }
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

    // Build EDID name map via SetupAPI (aligned with Windows Display Settings)
    m_edidNameMap = BuildEdidNameMap();
    WriteLog(L"EDID name map built: %zu entries", m_edidNameMap.size());
    {
        WCHAR diagBuf[128];
        swprintf_s(diagBuf, L"EDID name map: %zu entries", m_edidNameMap.size());
        m_diagLog.push_back(diagBuf);
        for (auto& [path, name] : m_edidNameMap) {
            swprintf_s(diagBuf, L"  EDID: \"%s\" -> path", name.c_str());
            m_diagLog.push_back(diagBuf);
        }
    }

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
