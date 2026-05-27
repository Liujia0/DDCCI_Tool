#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <string>
#include <vector>
#include <cstdint>

struct MonitorInfo {
    HANDLE hPhysicalMonitor;
    std::wstring name;
};

struct VCPFeature {
    uint32_t current;
    uint32_t max;
};

class MonitorManager {
public:
    MonitorManager() = default;
    ~MonitorManager();

    MonitorManager(const MonitorManager&) = delete;
    MonitorManager& operator=(const MonitorManager&) = delete;

    int EnumerateMonitors();
    int GetMonitorCount() const;
    const MonitorInfo* GetMonitor(int index) const;

    std::wstring GetCapabilities(int index);
    DWORD GetCapabilitiesStringLen(int index);
    std::vector<uint8_t> GetSupportedVCPCodes(int index);
    VCPFeature GetVCPFeature(int index, uint8_t vcpCode);
    void SetVCP(int index, uint8_t vcpCode, uint32_t value);

    void DestroyMonitors();

    std::vector<std::wstring> m_diagLog;

private:
    friend BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC hdc, LPRECT lprc, LPARAM dwData);
    void AddMonitor(HANDLE hPhysicalMonitor, const std::wstring& name);

    std::vector<MonitorInfo> m_monitors;
};
