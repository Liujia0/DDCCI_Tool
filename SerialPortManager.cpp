#include "SerialPortManager.h"

#include <algorithm>
#include <cstdarg>
#include <cwctype>
#include <cstdio>
#include <map>
#include <vector>

namespace {

using I2CDEV_PTR = void*;

using PFN_i2c_driver_init = I2CDEV_PTR (*)(const uint8_t* magic);
using PFN_i2c_driver_deinit = void (*)(I2CDEV_PTR handle);
using PFN_i2c_driver_scan = bool (*)(I2CDEV_PTR handle, intptr_t* count);
using PFN_i2c_driver_get_scan_result = bool (*)(I2CDEV_PTR handle, int index, uint8_t* buf, int size);
using PFN_i2c_driver_get_supported_device = bool (*)(I2CDEV_PTR handle, int index, uint8_t* buf, int size);
using PFN_i2c_driver_get_last_error = const char* (*)();
using PFN_i2c_driver_open = bool (*)(I2CDEV_PTR handle, const char* device);
using PFN_i2c_driver_close = void (*)(I2CDEV_PTR handle);
using PFN_i2c_driver_set_speed = bool (*)(I2CDEV_PTR handle, uint32_t speed);
using PFN_i2c_driver_set_enable_bulk = bool (*)(I2CDEV_PTR handle, bool enable);
using PFN_i2c_driver_read = bool (*)(I2CDEV_PTR handle, uint8_t addr, uint8_t* buf, intptr_t* len);
using PFN_i2c_driver_read_ddcci_auto = bool (*)(I2CDEV_PTR handle, uint8_t addr, uint8_t* buf, uint8_t* len);
using PFN_i2c_driver_write = bool (*)(I2CDEV_PTR handle, uint8_t addr, uint8_t* buf, intptr_t* len);
using PFN_i2c_driver_write_read_restart = bool (*)(I2CDEV_PTR handle, uint8_t addr, uint8_t* writeBuf, intptr_t* writeLen, intptr_t* readLen, uint8_t* readBuf);

HMODULE g_i2cDevModule = nullptr;
I2CDEV_PTR g_i2cDevContext = nullptr;
PFN_i2c_driver_init g_i2c_driver_init = nullptr;
PFN_i2c_driver_deinit g_i2c_driver_deinit = nullptr;
PFN_i2c_driver_scan g_i2c_driver_scan = nullptr;
PFN_i2c_driver_get_scan_result g_i2c_driver_get_scan_result = nullptr;
PFN_i2c_driver_get_supported_device g_i2c_driver_get_supported_device = nullptr;
PFN_i2c_driver_get_last_error g_i2c_driver_get_last_error = nullptr;
PFN_i2c_driver_open g_i2c_driver_open = nullptr;
PFN_i2c_driver_close g_i2c_driver_close = nullptr;
PFN_i2c_driver_set_speed g_i2c_driver_set_speed = nullptr;
PFN_i2c_driver_set_enable_bulk g_i2c_driver_set_enable_bulk = nullptr;
PFN_i2c_driver_read g_i2c_driver_read = nullptr;
PFN_i2c_driver_read_ddcci_auto g_i2c_driver_read_ddcci_auto = nullptr;
PFN_i2c_driver_write g_i2c_driver_write = nullptr;
PFN_i2c_driver_write_read_restart g_i2c_driver_write_read_restart = nullptr;

constexpr uint8_t I2C_DEV_MAGIC[256] = {
    0x07, 0x06, 0x1C, 0x16, 0x55, 0x55, 0x48, 0x64, 0x7B, 0x62, 0x66, 0x7B, 0x65, 0x75, 0x7D, 0x36,
    0x36, 0x63, 0x63, 0x34, 0x31, 0x61, 0x63, 0x6D, 0x75, 0x67, 0x65, 0x67, 0x66, 0x78, 0x64, 0x65,
    0x78, 0x65, 0x66, 0x7C, 0x23, 0x55, 0x5F, 0x40, 0x9C, 0x9E, 0xCB, 0x8A, 0x6E, 0x70, 0xB8, 0x0F,
    0x31, 0x61, 0x28, 0xB7, 0xE0, 0xD3, 0x06, 0x40, 0x9C, 0x9E, 0xCB, 0x8A, 0x6E, 0x70, 0xB8, 0xED,
    0x11, 0x6F, 0x85, 0x1B, 0xC5, 0x16, 0xBA, 0x40, 0x9C, 0x9E, 0xCB, 0x8A, 0x6E, 0x70, 0xB8, 0x0F,
    0x55, 0x4F, 0xC8, 0x3B, 0xD3, 0x50, 0x01, 0x40, 0x9C, 0x9E, 0xCB, 0x8A, 0x6E, 0x70, 0xB8, 0x49,
    0x62, 0x0E, 0x60, 0xA1, 0xA5, 0x5A, 0x3A, 0x40, 0x9C, 0x9E, 0xCB, 0x8A, 0x6E, 0x70, 0xB8, 0x42,
    0xFB, 0xAD, 0x7E, 0x3B, 0x68, 0xF7, 0xEE, 0x40, 0x9C, 0x9E, 0xCB, 0x8A, 0x6E, 0x70, 0xB8, 0x89,
    0x0F, 0xB9, 0xCD, 0xD7, 0x7B, 0xCF, 0x2B, 0x40, 0x9C, 0x9E, 0xCB, 0x8A, 0x6E, 0x70, 0xB8, 0x4B,
    0x84, 0x46, 0xA7, 0x01, 0xB2, 0x7D, 0x7B, 0x40, 0x9C, 0x9E, 0xCB, 0x8A, 0x6E, 0x70, 0xB8, 0x8B,
    0x79, 0x50, 0x00, 0xD1, 0xB7, 0x77, 0x6B, 0x40, 0x9C, 0x9E, 0xCB, 0x8A, 0x6E, 0x70, 0xB8, 0x7C,
    0x7A, 0x93, 0x6E, 0xF7, 0x62, 0xD4, 0x25, 0x40, 0x9C, 0x9E, 0xCB, 0x8A, 0x6E, 0x70, 0xB8, 0x45,
    0x58, 0xFC, 0x4D, 0x19, 0xEE, 0x31, 0x15, 0x55, 0x55, 0x55, 0x51, 0x40, 0x9C, 0x9E, 0xCB, 0x8A,
    0x6E, 0x70, 0xB8, 0x0F, 0x55, 0x4F, 0xC8, 0x3B, 0xD3, 0x50, 0x01, 0x40, 0x9C, 0x9E, 0xCB, 0x8A,
    0x6E, 0x70, 0xB8, 0x7C, 0x7A, 0x93, 0x6E, 0xF7, 0x62, 0xD4, 0x25, 0x40, 0x9C, 0x9E, 0xCB, 0x8A,
    0x6E, 0x70, 0xB8, 0x4D, 0xB0, 0x86, 0x19, 0x1B, 0xA7, 0x03, 0x06, 0x40, 0x9C, 0x9E, 0xCB, 0x8A
};

constexpr uint8_t DDC_WRITE_ADDR = 0x6E;
constexpr uint8_t DDC_READ_ADDR = 0x6F;
constexpr uint8_t DDC_7BIT_ADDR = 0x37;
constexpr uint8_t DDC_HOST_ADDR = 0x51;
constexpr uint8_t DDC_REPLY_CHECKSUM_SEED_ALT = 0x50;
constexpr wchar_t I2C_DEV_PORT_PREFIX[] = L"i2cdev:";

std::map<std::wstring, std::wstring> g_i2cDevScanNameMap;

I2CDEV_PTR SafeI2CDriverInit(DWORD* sehCode) {
    if (sehCode) *sehCode = 0;
    __try {
        return g_i2c_driver_init(I2C_DEV_MAGIC);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (sehCode) *sehCode = GetExceptionCode();
        return nullptr;
    }
}

bool SafeI2CDriverScan(I2CDEV_PTR handle, intptr_t* count, DWORD* sehCode) {
    if (sehCode) *sehCode = 0;
    __try {
        return g_i2c_driver_scan(handle, count);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (sehCode) *sehCode = GetExceptionCode();
        return false;
    }
}

bool SafeI2CDriverGetScanResult(I2CDEV_PTR handle, int index, uint8_t* buf, int size, DWORD* sehCode) {
    if (sehCode) *sehCode = 0;
    __try {
        return g_i2c_driver_get_scan_result(handle, index, buf, size);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (sehCode) *sehCode = GetExceptionCode();
        return false;
    }
}

bool SafeI2CDriverGetSupportedDevice(I2CDEV_PTR handle, int index, uint8_t* buf, int size, DWORD* sehCode) {
    if (sehCode) *sehCode = 0;
    __try {
        return g_i2c_driver_get_supported_device(handle, index, buf, size);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (sehCode) *sehCode = GetExceptionCode();
        return false;
    }
}

bool SafeI2CDriverOpen(I2CDEV_PTR handle, const char* device, DWORD* sehCode) {
    if (sehCode) *sehCode = 0;
    __try {
        return g_i2c_driver_open(handle, device);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (sehCode) *sehCode = GetExceptionCode();
        return false;
    }
}

bool SafeI2CDriverSetSpeed(I2CDEV_PTR handle, uint32_t speed, DWORD* sehCode) {
    if (sehCode) *sehCode = 0;
    __try {
        return g_i2c_driver_set_speed(handle, speed);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (sehCode) *sehCode = GetExceptionCode();
        return false;
    }
}

bool SafeI2CDriverSetEnableBulk(I2CDEV_PTR handle, bool enable, DWORD* sehCode) {
    if (sehCode) *sehCode = 0;
    __try {
        return g_i2c_driver_set_enable_bulk(handle, enable);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (sehCode) *sehCode = GetExceptionCode();
        return false;
    }
}

bool SafeI2CDriverClose(I2CDEV_PTR handle, DWORD* sehCode) {
    if (sehCode) *sehCode = 0;
    __try {
        g_i2c_driver_close(handle);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (sehCode) *sehCode = GetExceptionCode();
        return false;
    }
}

bool SafeI2CDriverWriteReadRestart(I2CDEV_PTR handle, uint8_t addr, uint8_t* writeBuf,
                                   intptr_t* writeLen, intptr_t* readLen, uint8_t* readBuf,
                                   DWORD* sehCode) {
    if (sehCode) *sehCode = 0;
    __try {
        return g_i2c_driver_write_read_restart(handle, addr, writeBuf, writeLen, readLen, readBuf);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (sehCode) *sehCode = GetExceptionCode();
        return false;
    }
}

bool SafeI2CDriverWrite(I2CDEV_PTR handle, uint8_t addr, uint8_t* buf, intptr_t* len, DWORD* sehCode) {
    if (sehCode) *sehCode = 0;
    __try {
        return g_i2c_driver_write(handle, addr, buf, len);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (sehCode) *sehCode = GetExceptionCode();
        return false;
    }
}

bool SafeI2CDriverRead(I2CDEV_PTR handle, uint8_t addr, uint8_t* buf, intptr_t* len, DWORD* sehCode) {
    if (sehCode) *sehCode = 0;
    __try {
        return g_i2c_driver_read(handle, addr, buf, len);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (sehCode) *sehCode = GetExceptionCode();
        return false;
    }
}

bool SafeI2CDriverReadDdcciAuto(I2CDEV_PTR handle, uint8_t addr, uint8_t* buf, uint8_t* len, DWORD* sehCode) {
    if (sehCode) *sehCode = 0;
    __try {
        return g_i2c_driver_read_ddcci_auto(handle, addr, buf, len);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (sehCode) *sehCode = GetExceptionCode();
        return false;
    }
}

void SPM_WriteLog(const std::wstring& msg) {
#ifdef _DEBUG
    OutputDebugStringW(msg.c_str());
    OutputDebugStringW(L"\n");

    WCHAR path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring logPath(path);
    size_t lastSlash = logPath.find_last_of(L"\\/");
    if (lastSlash != std::wstring::npos) {
        logPath = logPath.substr(0, lastSlash);
    }
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
                                    line.data(), len, nullptr, nullptr);
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

void SPM_WriteLog(const WCHAR* fmt, ...) {
    WCHAR buf[512];
    va_list args;
    va_start(args, fmt);
    _vsnwprintf_s(buf, _TRUNCATE, fmt, args);
    va_end(args);
    SPM_WriteLog(std::wstring(buf));
}

std::wstring Utf8ToWide(const char* value) {
    if (!value || !*value) {
        return {};
    }

    int len = MultiByteToWideChar(CP_UTF8, 0, value, -1, nullptr, 0);
    if (len <= 1) {
        len = MultiByteToWideChar(CP_ACP, 0, value, -1, nullptr, 0);
        if (len <= 1) {
            return {};
        }
        std::wstring out(static_cast<size_t>(len), L'\0');
        MultiByteToWideChar(CP_ACP, 0, value, -1, out.data(), len);
        if (!out.empty() && out.back() == L'\0') {
            out.pop_back();
        }
        return out;
    }

    std::wstring out(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value, -1, out.data(), len);
    if (!out.empty() && out.back() == L'\0') {
        out.pop_back();
    }
    return out;
}

std::string WideToAnsi(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }

    int len = WideCharToMultiByte(CP_ACP, 0, value.c_str(), static_cast<int>(value.size()),
                                  nullptr, 0, nullptr, nullptr);
    if (len <= 0) {
        return {};
    }

    std::string out(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_ACP, 0, value.c_str(), static_cast<int>(value.size()),
                        out.data(), len, nullptr, nullptr);
    return out;
}

std::wstring BytesToHex(const std::vector<uint8_t>& bytes) {
    std::wstring out;
    for (size_t i = 0; i < bytes.size(); ++i) {
        WCHAR h[8];
        swprintf_s(h, L"%02X", bytes[i]);
        if (!out.empty()) {
            out += L' ';
        }
        out += h;
    }
    return out;
}

std::wstring GetI2CDevLastErrorText() {
    if (!g_i2c_driver_get_last_error) {
        return L"i2c_driver_get_last_error unavailable";
    }
    const char* err = g_i2c_driver_get_last_error();
    if (!err || !*err) {
        return L"(empty)";
    }
    return Utf8ToWide(err);
}

std::string BuildI2CDevErrorMessage(const char* action) {
    std::string message = action ? action : "i2c_dev error";
    const std::wstring lastError = GetI2CDevLastErrorText();
    if (!lastError.empty()) {
        message += ": ";
        message += WideToAnsi(lastError);
    }
    return message;
}

std::wstring StripI2CDevPrefix(const std::wstring& portName) {
    if (portName.rfind(I2C_DEV_PORT_PREFIX, 0) == 0) {
        return portName.substr(wcslen(I2C_DEV_PORT_PREFIX));
    }
    return portName;
}

void RememberI2CDevScanName(const std::wstring& normalizedName, const std::wstring& rawName) {
    if (normalizedName.empty() || rawName.empty()) {
        return;
    }
    g_i2cDevScanNameMap[normalizedName] = rawName;
}

std::wstring FindRawI2CDevScanName(const std::wstring& normalizedName) {
    const auto it = g_i2cDevScanNameMap.find(normalizedName);
    if (it == g_i2cDevScanNameMap.end()) {
        return {};
    }
    return it->second;
}

std::wstring NormalizeI2CDevDeviceName(const std::wstring& rawName) {
    std::wstring out;
    out.reserve(rawName.size());

    bool pendingSpace = false;
    for (wchar_t ch : rawName) {
        if (ch == L'\0') {
            break;
        }

        if (iswcntrl(static_cast<wint_t>(ch)) || iswspace(static_cast<wint_t>(ch))) {
            pendingSpace = !out.empty();
            continue;
        }

        if (pendingSpace) {
            if (!out.empty() && out.back() != L'(' && ch != L')') {
                out.push_back(L' ');
            }
            pendingSpace = false;
        }

        if (ch == L')' && !out.empty() && out.back() == L' ') {
            out.pop_back();
        }

        out.push_back(ch);
    }

    while (!out.empty() && out.back() == L' ') {
        out.pop_back();
    }
    return out;
}

std::wstring TrimTrailingSpaces(const std::wstring& value) {
    std::wstring out = value;
    while (!out.empty() && iswspace(static_cast<wint_t>(out.back()))) {
        out.pop_back();
    }
    return out;
}

std::wstring RemoveTrailingParentheticalSuffix(const std::wstring& value) {
    std::wstring trimmed = TrimTrailingSpaces(value);
    if (trimmed.empty() || trimmed.back() != L')') {
        return trimmed;
    }

    const size_t pos = trimmed.find_last_of(L'(');
    if (pos == std::wstring::npos) {
        return trimmed;
    }
    return TrimTrailingSpaces(trimmed.substr(0, pos));
}

bool IsDigitsOnly(const std::wstring& value) {
    if (value.empty()) {
        return false;
    }
    for (wchar_t ch : value) {
        if (!iswdigit(static_cast<wint_t>(ch))) {
            return false;
        }
    }
    return true;
}

void AppendUniqueDeviceName(std::vector<std::wstring>& values, const std::wstring& candidate) {
    if (candidate.empty()) {
        return;
    }
    if (std::find(values.begin(), values.end(), candidate) == values.end()) {
        values.push_back(candidate);
    }
}

std::vector<std::wstring> BuildOpenDeviceCandidates(const std::wstring& deviceName, bool isNvapiBackend) {
    std::vector<std::wstring> candidates;
    AppendUniqueDeviceName(candidates, deviceName);

    if (!isNvapiBackend) {
        return candidates;
    }

    const std::wstring noMonitorName = RemoveTrailingParentheticalSuffix(deviceName);
    AppendUniqueDeviceName(candidates, noMonitorName);

    const size_t lastColon = noMonitorName.find_last_of(L':');
    if (lastColon != std::wstring::npos) {
        const std::wstring suffix = NormalizeI2CDevDeviceName(noMonitorName.substr(lastColon + 1));
        if (IsDigitsOnly(suffix)) {
            AppendUniqueDeviceName(candidates, TrimTrailingSpaces(noMonitorName.substr(0, lastColon)));
        }
    }

    const size_t firstColon = deviceName.find(L':');
    if (firstColon != std::wstring::npos) {
        AppendUniqueDeviceName(candidates, TrimTrailingSpaces(deviceName.substr(0, firstColon)));
    }

    return candidates;
}

std::vector<std::wstring> EnumerateSupportedDeviceNames() {
    std::vector<std::wstring> names;
    if (!g_i2cDevContext || !g_i2c_driver_get_supported_device) {
        return names;
    }

    int consecutiveFailures = 0;
    for (int index = 0; index < 64 && consecutiveFailures < 8; ++index) {
        std::vector<uint8_t> nameBuf(1024, 0);
        DWORD sehCode = 0;
        const bool ok = SafeI2CDriverGetSupportedDevice(g_i2cDevContext, index, nameBuf.data(),
                                                        static_cast<int>(nameBuf.size()), &sehCode);
        if (sehCode != 0) {
            SPM_WriteLog(L"EnumerateSupportedDeviceNames: get_supported_device(%d) raised SEH 0x%08X",
                         index, sehCode);
            break;
        }

        if (!ok) {
            ++consecutiveFailures;
            continue;
        }

        consecutiveFailures = 0;
        nameBuf.back() = 0;
        std::wstring rawName = Utf8ToWide(reinterpret_cast<const char*>(nameBuf.data()));
        std::wstring name = NormalizeI2CDevDeviceName(rawName);
        if (name.empty()) {
            continue;
        }
        AppendUniqueDeviceName(names, name);
    }

    return names;
}

bool ContainsNoCase(const std::wstring& text, const wchar_t* needle) {
    if (!needle || !*needle) {
        return false;
    }

    std::wstring haystack = text;
    std::wstring probe = needle;
    std::transform(haystack.begin(), haystack.end(), haystack.begin(), towlower);
    std::transform(probe.begin(), probe.end(), probe.begin(), towlower);
    return haystack.find(probe) != std::wstring::npos;
}

bool IsNonFatalCapabilityError(const std::wstring& errorText) {
    return ContainsNoCase(errorText, L"not support") ||
           ContainsNoCase(errorText, L"not supported") ||
           ContainsNoCase(errorText, L"unsupported");
}

bool IsWriteOnlyPreferredRawBody(const std::vector<uint8_t>& txBody) {
    if (txBody.size() >= 2 && txBody[0] == 0x01) {
        return false;
    }
    if (txBody.size() >= 4 && txBody[0] == 0x03) {
        return false;
    }
    if (txBody.size() >= 3 && txBody[0] == 0xF3) {
        return false;
    }
    return true;
}

bool IsValidDDCChecksum(const std::vector<uint8_t>& packet, uint8_t* matchedSeed = nullptr) {
    if (packet.size() < 2) {
        return false;
    }

    const uint8_t seeds[] = { DDC_READ_ADDR, DDC_REPLY_CHECKSUM_SEED_ALT };
    for (uint8_t seed : seeds) {
        uint8_t checksum = seed;
        for (size_t i = 0; i + 1 < packet.size(); ++i) {
            checksum ^= packet[i];
        }
        if (checksum == packet.back()) {
            if (matchedSeed) {
                *matchedSeed = seed;
            }
            return true;
        }
    }
    return false;
}

} // namespace

bool SerialPortManager::LoadI2CDev(const std::wstring& dllPath) {
    if (g_i2cDevModule) {
        return true;
    }

    g_i2cDevModule = LoadLibraryW(dllPath.c_str());
    if (!g_i2cDevModule) {
        SPM_WriteLog(L"LoadI2CDev: LoadLibraryW(%s) failed, err=%u", dllPath.c_str(), GetLastError());
        return false;
    }

#define RESOLVE_I2C_DEV(name) g_##name = reinterpret_cast<PFN_##name>(GetProcAddress(g_i2cDevModule, #name))
    RESOLVE_I2C_DEV(i2c_driver_init);
    RESOLVE_I2C_DEV(i2c_driver_deinit);
    RESOLVE_I2C_DEV(i2c_driver_scan);
    RESOLVE_I2C_DEV(i2c_driver_get_scan_result);
    RESOLVE_I2C_DEV(i2c_driver_get_supported_device);
    RESOLVE_I2C_DEV(i2c_driver_get_last_error);
    RESOLVE_I2C_DEV(i2c_driver_open);
    RESOLVE_I2C_DEV(i2c_driver_close);
    RESOLVE_I2C_DEV(i2c_driver_set_speed);
    RESOLVE_I2C_DEV(i2c_driver_set_enable_bulk);
    RESOLVE_I2C_DEV(i2c_driver_read);
    RESOLVE_I2C_DEV(i2c_driver_read_ddcci_auto);
    RESOLVE_I2C_DEV(i2c_driver_write);
    RESOLVE_I2C_DEV(i2c_driver_write_read_restart);
#undef RESOLVE_I2C_DEV

    if (!g_i2c_driver_init || !g_i2c_driver_scan || !g_i2c_driver_get_scan_result ||
        !g_i2c_driver_get_supported_device || !g_i2c_driver_get_last_error ||
        !g_i2c_driver_open || !g_i2c_driver_close || !g_i2c_driver_set_speed ||
        !g_i2c_driver_set_enable_bulk || !g_i2c_driver_read || !g_i2c_driver_write) {
        SPM_WriteLog(L"LoadI2CDev: failed to resolve critical exports");
        FreeLibrary(g_i2cDevModule);
        g_i2cDevModule = nullptr;
        g_i2c_driver_init = nullptr;
        g_i2c_driver_deinit = nullptr;
        g_i2c_driver_scan = nullptr;
        g_i2c_driver_get_scan_result = nullptr;
        g_i2c_driver_get_supported_device = nullptr;
        g_i2c_driver_get_last_error = nullptr;
        g_i2c_driver_open = nullptr;
        g_i2c_driver_close = nullptr;
        g_i2c_driver_set_speed = nullptr;
        g_i2c_driver_set_enable_bulk = nullptr;
        g_i2c_driver_read = nullptr;
        g_i2c_driver_read_ddcci_auto = nullptr;
        g_i2c_driver_write = nullptr;
        g_i2c_driver_write_read_restart = nullptr;
        return false;
    }

    SPM_WriteLog(L"LoadI2CDev: loaded from %s", dllPath.c_str());
    return true;
}

bool SerialPortManager::IsI2CDevLoaded() {
    return g_i2cDevModule != nullptr;
}

bool SerialPortManager::EnsureI2CDevReady() {
    if (!g_i2cDevModule || !g_i2c_driver_init) {
        SPM_WriteLog(L"EnsureI2CDevReady: i2c_dev.dll not loaded");
        return false;
    }

    if (g_i2cDevContext) {
        return true;
    }

    DWORD sehCode = 0;
    g_i2cDevContext = SafeI2CDriverInit(&sehCode);
    if (sehCode != 0) {
        SPM_WriteLog(L"EnsureI2CDevReady: i2c_driver_init raised SEH 0x%08X", sehCode);
    }
    if (!g_i2cDevContext) {
        SPM_WriteLog(L"EnsureI2CDevReady: i2c_driver_init failed: %s", GetI2CDevLastErrorText().c_str());
        return false;
    }

    SPM_WriteLog(L"EnsureI2CDevReady: i2c_driver_init succeeded, ctx=%p", g_i2cDevContext);
    return true;
}

SerialPortManager::~SerialPortManager() {
    ClosePort();
}

std::vector<SerialPortInfo> SerialPortManager::EnumerateI2CDevPorts() {
    std::vector<SerialPortInfo> ports;
    if (!EnsureI2CDevReady()) {
        return ports;
    }

    g_i2cDevScanNameMap.clear();

    intptr_t scanCount = 0;
    DWORD sehCode = 0;
    const bool scanOk = SafeI2CDriverScan(g_i2cDevContext, &scanCount, &sehCode);
    if (sehCode != 0) {
        SPM_WriteLog(L"EnumerateI2CDevPorts: i2c_driver_scan raised SEH 0x%08X", sehCode);
        return ports;
    }

    if (scanOk) {
        SPM_WriteLog(L"EnumerateI2CDevPorts: scan succeeded, count=%lld", static_cast<long long>(scanCount));
        for (int index = 0; index < static_cast<int>(scanCount); ++index) {
            std::vector<uint8_t> nameBuf(4096, 0);
            sehCode = 0;
            const bool resultOk = SafeI2CDriverGetScanResult(g_i2cDevContext, index, nameBuf.data(),
                                                             static_cast<int>(nameBuf.size()), &sehCode);
            if (sehCode != 0) {
                SPM_WriteLog(L"EnumerateI2CDevPorts: get_scan_result(%d) raised SEH 0x%08X", index, sehCode);
                continue;
            }

            if (!resultOk) {
                SPM_WriteLog(L"EnumerateI2CDevPorts: get_scan_result(%d) failed: %s",
                             index, GetI2CDevLastErrorText().c_str());
                continue;
            }

            nameBuf.back() = 0;
            const std::wstring rawDeviceName = Utf8ToWide(reinterpret_cast<const char*>(nameBuf.data()));
            std::wstring deviceName = NormalizeI2CDevDeviceName(rawDeviceName);
            if (deviceName.empty()) {
                continue;
            }

            RememberI2CDevScanName(deviceName, rawDeviceName);

            if (deviceName != rawDeviceName) {
                SPM_WriteLog(L"EnumerateI2CDevPorts: sanitized scan[%d] raw=[%s] normalized=[%s]",
                             index, rawDeviceName.c_str(), deviceName.c_str());
            }

            SerialPortInfo info;
            info.portName = std::wstring(I2C_DEV_PORT_PREFIX) + deviceName;
            info.description = deviceName + L" (i2c_dev)";
            ports.push_back(info);
            SPM_WriteLog(L"EnumerateI2CDevPorts: scan[%d] %s", index, deviceName.c_str());
        }
        return ports;
    }

    SPM_WriteLog(L"EnumerateI2CDevPorts: scan failed: %s", GetI2CDevLastErrorText().c_str());
    return ports;
}

std::vector<SerialPortInfo> SerialPortManager::EnumeratePorts() {
    return EnumerateI2CDevPorts();
}

bool SerialPortManager::OpenI2CDevPort(const std::wstring& portName, DWORD baudRate) {
    if (!EnsureI2CDevReady()) {
        return false;
    }

    const std::wstring deviceName = NormalizeI2CDevDeviceName(StripI2CDevPrefix(portName));
    const std::wstring rawScannedDeviceName = FindRawI2CDevScanName(deviceName);
    const bool isVendorGpuBackend = ContainsNoCase(deviceName, L"igcl") ||
                                    ContainsNoCase(deviceName, L"intel") ||
                                    ContainsNoCase(deviceName, L"igfxext") ||
                                    ContainsNoCase(deviceName, L"adl") ||
                                    ContainsNoCase(deviceName, L"adlx") ||
                                    ContainsNoCase(deviceName, L"amd") ||
                                    ContainsNoCase(deviceName, L"radeon") ||
                                    ContainsNoCase(deviceName, L"nvapi") ||
                                    ContainsNoCase(deviceName, L"nvidia") ||
                                    ContainsNoCase(deviceName, L"geforce");
    const bool isNvapiBackend = ContainsNoCase(deviceName, L"nvapi") ||
                                ContainsNoCase(deviceName, L"nvidia") ||
                                ContainsNoCase(deviceName, L"geforce");
    std::vector<std::wstring> openCandidates;
    if (isNvapiBackend) {
        AppendUniqueDeviceName(openCandidates, rawScannedDeviceName);
    }
    if (isNvapiBackend && !rawScannedDeviceName.empty() && rawScannedDeviceName != deviceName) {
        SPM_WriteLog(L"OpenI2CDevPort: raw scan name for %s is [%s]",
                     deviceName.c_str(), rawScannedDeviceName.c_str());
    }
    const std::vector<std::wstring> fallbackCandidates = BuildOpenDeviceCandidates(deviceName, isNvapiBackend);
    for (const std::wstring& candidate : fallbackCandidates) {
        AppendUniqueDeviceName(openCandidates, candidate);
    }
    if (isNvapiBackend) {
        const std::vector<std::wstring> supportedDevices = EnumerateSupportedDeviceNames();
        for (size_t i = 0; i < supportedDevices.size(); ++i) {
            const std::wstring& supported = supportedDevices[i];
            SPM_WriteLog(L"OpenI2CDevPort: supported_device[%zu]=%s", i, supported.c_str());
            if (ContainsNoCase(supported, L"nvapi") ||
                ContainsNoCase(supported, L"nvidia") ||
                ContainsNoCase(supported, L"geforce")) {
                AppendUniqueDeviceName(openCandidates, supported);
            }
        }
    }
    if (openCandidates.empty()) {
        SPM_WriteLog(L"OpenI2CDevPort: empty device name after conversion");
        return false;
    }

    std::wstring openedDeviceName;
    std::wstring openErrorText;
    for (const std::wstring& candidate : openCandidates) {
        const std::string deviceNameAnsi = WideToAnsi(candidate);
        if (deviceNameAnsi.empty()) {
            continue;
        }

        SPM_WriteLog(L"OpenI2CDevPort: trying i2c_driver_open(%s)", candidate.c_str());
        DWORD sehCode = 0;
        const bool openOk = SafeI2CDriverOpen(g_i2cDevContext, deviceNameAnsi.c_str(), &sehCode);
        if (sehCode != 0) {
            SPM_WriteLog(L"OpenI2CDevPort: i2c_driver_open(%s) raised SEH 0x%08X",
                         candidate.c_str(), sehCode);
            return false;
        }

        if (openOk) {
            openedDeviceName = candidate;
            break;
        }

        openErrorText = GetI2CDevLastErrorText();
        SPM_WriteLog(L"OpenI2CDevPort: i2c_driver_open(%s) failed: %s",
                     candidate.c_str(), openErrorText.c_str());
        const bool canTryNextNvapiCandidate = isNvapiBackend &&
                                              ContainsNoCase(openErrorText, L"device not found at open");
        if (!canTryNextNvapiCandidate && candidate != openCandidates.back()) {
            SPM_WriteLog(L"OpenI2CDevPort: candidate failed for %s, continue with next candidate",
                         deviceName.c_str());
        }
    }

    if (openedDeviceName.empty()) {
        return false;
    }

    uint32_t speed = (baudRate > 0 && baudRate <= 1000) ? (1000u / baudRate) : 100000u;
    DWORD sehCode = 0;
    const bool speedOk = SafeI2CDriverSetSpeed(g_i2cDevContext, speed, &sehCode);
    if (sehCode != 0) {
        SPM_WriteLog(L"OpenI2CDevPort: i2c_driver_set_speed(%u) raised SEH 0x%08X", speed, sehCode);
        if (g_i2c_driver_close) {
            g_i2c_driver_close(g_i2cDevContext);
        }
        return false;
    }

    if (!speedOk) {
        const std::wstring speedError = GetI2CDevLastErrorText();
        if (isVendorGpuBackend && IsNonFatalCapabilityError(speedError)) {
            SPM_WriteLog(L"OpenI2CDevPort: i2c_driver_set_speed(%u) unsupported for %s, continue without speed config",
                         speed, deviceName.c_str());
        } else {
            SPM_WriteLog(L"OpenI2CDevPort: i2c_driver_set_speed(%u) failed: %s",
                         speed, speedError.c_str());
            if (g_i2c_driver_close) {
                g_i2c_driver_close(g_i2cDevContext);
            }
            return false;
        }
    }

    sehCode = 0;
    const bool bulkOk = SafeI2CDriverSetEnableBulk(g_i2cDevContext, false, &sehCode);
    if (sehCode != 0) {
        SPM_WriteLog(L"OpenI2CDevPort: i2c_driver_set_enable_bulk(false) raised SEH 0x%08X", sehCode);
        if (g_i2c_driver_close) {
            g_i2c_driver_close(g_i2cDevContext);
        }
        return false;
    }

    if (!bulkOk) {
        const std::wstring bulkError = GetI2CDevLastErrorText();
        if (isVendorGpuBackend && IsNonFatalCapabilityError(bulkError)) {
            SPM_WriteLog(L"OpenI2CDevPort: i2c_driver_set_enable_bulk(false) unsupported for %s, continue without bulk config",
                         deviceName.c_str());
        } else {
            SPM_WriteLog(L"OpenI2CDevPort: i2c_driver_set_enable_bulk(false) failed: %s",
                         bulkError.c_str());
            if (g_i2c_driver_close) {
                g_i2c_driver_close(g_i2cDevContext);
            }
            return false;
        }
    }

    m_deviceName = std::wstring(I2C_DEV_PORT_PREFIX) + deviceName;
    SPM_WriteLog(L"OpenI2CDevPort: %s opened via i2c_dev using candidate [%s] (speed=%u)",
                 deviceName.c_str(), openedDeviceName.c_str(), speed);
    return true;
}

bool SerialPortManager::OpenPort(const std::wstring& portName, DWORD baudRate) {
    ClosePort();
    return OpenI2CDevPort(portName, baudRate);
}

void SerialPortManager::ClosePort() {
    if (!m_deviceName.empty() && g_i2cDevContext && g_i2c_driver_close) {
        std::wstring opened = StripI2CDevPrefix(m_deviceName);
        DWORD sehCode = 0;
        if (SafeI2CDriverClose(g_i2cDevContext, &sehCode)) {
            SPM_WriteLog(L"ClosePort: %s closed via i2c_dev", opened.c_str());
        } else if (sehCode != 0) {
            SPM_WriteLog(L"ClosePort: i2c_driver_close(%s) raised SEH 0x%08X", opened.c_str(), sehCode);
        }
    }
    m_deviceName.clear();
}

bool SerialPortManager::IsOpen() const {
    return !m_deviceName.empty();
}

std::wstring SerialPortManager::GetOpenPortName() const {
    return m_deviceName;
}

void SerialPortManager::SetRawReadWaitMs(DWORD waitMs) {
    m_rawReadWaitMs = waitMs;
}

DWORD SerialPortManager::GetRawReadWaitMs() const {
    return m_rawReadWaitMs;
}

void SerialPortManager::WaitBeforeRawRead(const wchar_t* stage) const {
    const DWORD waitMs = GetRawReadWaitMs();
    SPM_WriteLog(L"%s: wait %lu ms before reply read",
                 stage ? stage : L"WaitBeforeRawRead",
                 static_cast<unsigned long>(waitMs));
    if (waitMs > 0) {
        Sleep(waitMs);
    }
}

bool SerialPortManager::IsRestartPreferredDevice() const {
    const std::wstring deviceName = StripI2CDevPrefix(m_deviceName);
    return ContainsNoCase(deviceName, L"igcl") ||
           ContainsNoCase(deviceName, L"adl") ||
           ContainsNoCase(deviceName, L"adlx") ||
           ContainsNoCase(deviceName, L"nvapi") ||
           ContainsNoCase(deviceName, L"intel") ||
           ContainsNoCase(deviceName, L"amd") ||
           ContainsNoCase(deviceName, L"radeon") ||
           ContainsNoCase(deviceName, L"nvidia") ||
           ContainsNoCase(deviceName, L"geforce") ||
           ContainsNoCase(deviceName, L"igfxext");
}

bool SerialPortManager::IsAutoReadPreferredDevice() const {
    const std::wstring deviceName = StripI2CDevPrefix(m_deviceName);
    return ContainsNoCase(deviceName, L"igcl") ||
           ContainsNoCase(deviceName, L"intel") ||
           ContainsNoCase(deviceName, L"igfxext");
}

bool SerialPortManager::IsValidDDCReply(const std::vector<uint8_t>& packet, std::string& reason) const {
    if (packet.size() < 4) {
        reason = "reply too short";
        return false;
    }

    if (packet[0] != DDC_WRITE_ADDR) {
        char buf[64];
        sprintf_s(buf, "unexpected target byte 0x%02X", packet[0]);
        reason = buf;
        return false;
    }

    if ((packet[1] & 0x80) == 0) {
        char buf[64];
        sprintf_s(buf, "length byte missing high bit: 0x%02X", packet[1]);
        reason = buf;
        return false;
    }

    const size_t expectedSize = static_cast<size_t>(packet[1] & 0x7F) + 3;
    if (packet.size() != expectedSize) {
        char buf[64];
        sprintf_s(buf, "reply size mismatch (%zu != %zu)", packet.size(), expectedSize);
        reason = buf;
        return false;
    }

    if (!IsValidDDCChecksum(packet)) {
        uint8_t checksumReadSeed = DDC_READ_ADDR;
        for (size_t i = 0; i + 1 < packet.size(); ++i) {
            checksumReadSeed ^= packet[i];
        }
        uint8_t checksumAltSeed = DDC_REPLY_CHECKSUM_SEED_ALT;
        for (size_t i = 0; i + 1 < packet.size(); ++i) {
            checksumAltSeed ^= packet[i];
        }
        char buf[80];
        sprintf_s(buf, "checksum mismatch (calc=0x%02X/0x%02X recv=0x%02X)",
                  checksumReadSeed, checksumAltSeed, packet.back());
        reason = buf;
        return false;
    }

    reason.clear();
    return true;
}

bool SerialPortManager::ExtractFirstValidDDCReply(const std::vector<uint8_t>& rawData,
                                                  std::vector<uint8_t>& packet,
                                                  std::string& reason) const {
    packet.clear();
    reason = "no valid DDC reply found in buffer";
    if (rawData.size() < 4) {
        reason = "raw buffer too short";
        return false;
    }

    std::string lastCandidateReason;
    const size_t searchLimit = (std::min)(rawData.size(), static_cast<size_t>(32));
    for (size_t start = 0; start + 4 <= searchLimit; ++start) {
        if (rawData[start] != DDC_WRITE_ADDR) {
            continue;
        }

        const uint8_t lenByte = rawData[start + 1];
        if ((lenByte & 0x80) == 0) {
            continue;
        }

        const size_t packetSize = static_cast<size_t>(lenByte & 0x7F) + 3;
        if (start + packetSize > rawData.size()) {
            continue;
        }

        std::vector<uint8_t> candidate(rawData.begin() + start, rawData.begin() + start + packetSize);
        std::string candidateReason;
        if (IsValidDDCReply(candidate, candidateReason)) {
            packet = std::move(candidate);
            reason.clear();
            return true;
        }
        lastCandidateReason = candidateReason;
    }

    // Some GPU backends return the DDC payload without the leading 0x6E target byte.
    // Normalize that shortened form back into a canonical DDC reply packet.
    for (size_t start = 0; start + 3 <= searchLimit; ++start) {
        const uint8_t lenByte = rawData[start];
        if ((lenByte & 0x80) == 0) {
            continue;
        }

        const size_t packetSizeWithoutTarget = static_cast<size_t>(lenByte & 0x7F) + 2;
        if (start + packetSizeWithoutTarget > rawData.size()) {
            continue;
        }

        std::vector<uint8_t> candidate;
        candidate.reserve(packetSizeWithoutTarget + 1);
        candidate.push_back(DDC_WRITE_ADDR);
        candidate.insert(candidate.end(),
                         rawData.begin() + start,
                         rawData.begin() + start + packetSizeWithoutTarget);

        std::string candidateReason;
        if (IsValidDDCReply(candidate, candidateReason)) {
            packet = std::move(candidate);
            reason.clear();
            return true;
        }
        lastCandidateReason = candidateReason;
    }

    if (!lastCandidateReason.empty()) {
        reason += ": ";
        reason += lastCandidateReason;
    }

    return false;
}

bool SerialPortManager::DDCSendRawI2CDevWriteReadRestart(const std::vector<uint8_t>& txPacket,
                                                         std::vector<uint8_t>& rxData,
                                                         std::string& error) {
    if (!g_i2c_driver_write_read_restart) {
        error = "i2c_driver_write_read_restart unavailable";
        return false;
    }

    std::vector<uint8_t> readBuf(512, 0);
    intptr_t txLen = static_cast<intptr_t>(txPacket.size());
    intptr_t rxLen = static_cast<intptr_t>(readBuf.size());

    SPM_WriteLog(L"DDCSendRawI2CDev[restart]: addr=0x%02X txLen=%lld rxCap=%lld data=[%s]",
                 DDC_7BIT_ADDR,
                 static_cast<long long>(txLen),
                 static_cast<long long>(rxLen),
                 BytesToHex(txPacket).c_str());

    DWORD sehCode = 0;
    const bool restartOk = SafeI2CDriverWriteReadRestart(g_i2cDevContext, DDC_7BIT_ADDR,
                                                         const_cast<uint8_t*>(txPacket.data()),
                                                         &txLen, &rxLen, readBuf.data(), &sehCode);
    if (sehCode != 0) {
        SPM_WriteLog(L"DDCSendRawI2CDev[restart]: i2c_driver_write_read_restart raised SEH 0x%08X", sehCode);
        error = "i2c_driver_write_read_restart raised SEH";
        return false;
    }

    if (!restartOk) {
        const std::wstring lastError = GetI2CDevLastErrorText();
        SPM_WriteLog(L"DDCSendRawI2CDev[restart]: failed, err=%s", lastError.c_str());
        error = BuildI2CDevErrorMessage("i2c_driver_write_read_restart failed");
        return false;
    }

    SPM_WriteLog(L"DDCSendRawI2CDev[restart]: ok txLen=%lld rxLen=%lld",
                 static_cast<long long>(txLen), static_cast<long long>(rxLen));
    if (rxLen <= 0) {
        error = "i2c_driver_write_read_restart returned no data";
        return false;
    }
    if (rxLen > static_cast<intptr_t>(readBuf.size())) {
        rxLen = static_cast<intptr_t>(readBuf.size());
    }

    std::vector<uint8_t> rawData(readBuf.begin(), readBuf.begin() + static_cast<size_t>(rxLen));
    if (!ExtractFirstValidDDCReply(rawData, rxData, error)) {
        SPM_WriteLog(L"DDCSendRawI2CDev[restart]: invalid reply: %S raw=[%s]",
                     error.c_str(), BytesToHex(rawData).c_str());
        return false;
    }

    SPM_WriteLog(L"DDCSendRawI2CDev[restart]: read ok len=%zu raw=[%s]",
                 rxData.size(), BytesToHex(rxData).c_str());
    return true;
}

bool SerialPortManager::DDCSendRawI2CDevWriteThenRead(const std::vector<uint8_t>& txPacket,
                                                      std::vector<uint8_t>& rxData,
                                                      std::string& error) {
    intptr_t txLen = static_cast<intptr_t>(txPacket.size());
    SPM_WriteLog(L"DDCSendRawI2CDev[write/read]: write addr=0x%02X len=%lld data=[%s]",
                 DDC_7BIT_ADDR, static_cast<long long>(txLen), BytesToHex(txPacket).c_str());
    DWORD sehCode = 0;
    const bool writeOk = SafeI2CDriverWrite(g_i2cDevContext, DDC_7BIT_ADDR,
                                            const_cast<uint8_t*>(txPacket.data()), &txLen, &sehCode);
    if (sehCode != 0) {
        SPM_WriteLog(L"DDCSendRawI2CDev[write/read]: i2c_driver_write raised SEH 0x%08X", sehCode);
        error = "i2c_driver_write raised SEH";
        return false;
    }

    if (!writeOk) {
        const std::wstring lastError = GetI2CDevLastErrorText();
        SPM_WriteLog(L"DDCSendRawI2CDev[write/read]: i2c_driver_write failed, err=%s",
                     lastError.c_str());
        error = BuildI2CDevErrorMessage("i2c_driver_write failed");
        return false;
    }

    SPM_WriteLog(L"DDCSendRawI2CDev[write/read]: write ok, transferred=%lld", static_cast<long long>(txLen));
    WaitBeforeRawRead(L"DDCSendRawI2CDev[write/read]");

    std::vector<uint8_t> readBuf(512, 0);
    intptr_t rxLen = static_cast<intptr_t>(readBuf.size());
    SPM_WriteLog(L"DDCSendRawI2CDev[write/read]: read addr=0x%02X requestCapacity=%lld",
                 DDC_7BIT_ADDR, static_cast<long long>(rxLen));
    sehCode = 0;
    const bool readOk = SafeI2CDriverRead(g_i2cDevContext, DDC_7BIT_ADDR, readBuf.data(), &rxLen, &sehCode);
    if (sehCode != 0) {
        SPM_WriteLog(L"DDCSendRawI2CDev[write/read]: i2c_driver_read raised SEH 0x%08X", sehCode);
        error = "i2c_driver_read raised SEH";
        return false;
    }

    if (!readOk) {
        const std::wstring lastError = GetI2CDevLastErrorText();
        SPM_WriteLog(L"DDCSendRawI2CDev[write/read]: i2c_driver_read failed, err=%s",
                     lastError.c_str());
        error = BuildI2CDevErrorMessage("i2c_driver_read failed");
        return false;
    }

    SPM_WriteLog(L"DDCSendRawI2CDev[write/read]: read returned result=1 rxLen=%lld",
                 static_cast<long long>(rxLen));
    if (rxLen <= 0) {
        error = "i2c_driver_read returned no data";
        return false;
    }
    if (rxLen > static_cast<intptr_t>(readBuf.size())) {
        rxLen = static_cast<intptr_t>(readBuf.size());
    }

    std::vector<uint8_t> rawData(readBuf.begin(), readBuf.begin() + static_cast<size_t>(rxLen));
    if (!ExtractFirstValidDDCReply(rawData, rxData, error)) {
        SPM_WriteLog(L"DDCSendRawI2CDev[write/read]: invalid reply: %S raw=[%s]",
                     error.c_str(), BytesToHex(rawData).c_str());
        return false;
    }

    SPM_WriteLog(L"DDCSendRawI2CDev[write/read]: read ok len=%zu raw=[%s]",
                 rxData.size(), BytesToHex(rxData).c_str());
    return true;
}

bool SerialPortManager::DDCSendRawI2CDevWriteThenAutoRead(const std::vector<uint8_t>& txPacket,
                                                          std::vector<uint8_t>& rxData,
                                                          std::string& error) {
    if (!g_i2c_driver_read_ddcci_auto) {
        error = "i2c_driver_read_ddcci_auto unavailable";
        return false;
    }

    intptr_t txLen = static_cast<intptr_t>(txPacket.size());
    SPM_WriteLog(L"DDCSendRawI2CDev[auto-read]: write addr=0x%02X len=%lld data=[%s]",
                 DDC_7BIT_ADDR, static_cast<long long>(txLen), BytesToHex(txPacket).c_str());
    DWORD sehCode = 0;
    const bool writeOk = SafeI2CDriverWrite(g_i2cDevContext, DDC_7BIT_ADDR,
                                            const_cast<uint8_t*>(txPacket.data()), &txLen, &sehCode);
    if (sehCode != 0) {
        SPM_WriteLog(L"DDCSendRawI2CDev[auto-read]: i2c_driver_write raised SEH 0x%08X", sehCode);
        error = "i2c_driver_write raised SEH";
        return false;
    }

    if (!writeOk) {
        const std::wstring lastError = GetI2CDevLastErrorText();
        SPM_WriteLog(L"DDCSendRawI2CDev[auto-read]: i2c_driver_write failed, err=%s",
                     lastError.c_str());
        error = BuildI2CDevErrorMessage("i2c_driver_write failed");
        return false;
    }

    WaitBeforeRawRead(L"DDCSendRawI2CDev[auto-read]");

    std::vector<uint8_t> readBuf(255, 0);
    uint8_t rxLen = static_cast<uint8_t>(readBuf.size());
    SPM_WriteLog(L"DDCSendRawI2CDev[auto-read]: read_ddcci_auto addr=0x%02X requestCapacity=%u",
                 DDC_7BIT_ADDR, static_cast<unsigned>(rxLen));
    sehCode = 0;
    const bool autoReadOk = SafeI2CDriverReadDdcciAuto(g_i2cDevContext, DDC_7BIT_ADDR,
                                                       readBuf.data(), &rxLen, &sehCode);
    if (sehCode != 0) {
        SPM_WriteLog(L"DDCSendRawI2CDev[auto-read]: i2c_driver_read_ddcci_auto raised SEH 0x%08X", sehCode);
        error = "i2c_driver_read_ddcci_auto raised SEH";
        return false;
    }

    if (!autoReadOk) {
        const std::wstring lastError = GetI2CDevLastErrorText();
        SPM_WriteLog(L"DDCSendRawI2CDev[auto-read]: i2c_driver_read_ddcci_auto failed, err=%s",
                     lastError.c_str());
        error = BuildI2CDevErrorMessage("i2c_driver_read_ddcci_auto failed");
        return false;
    }

    SPM_WriteLog(L"DDCSendRawI2CDev[auto-read]: read returned result=1 rxLen=%u",
                 static_cast<unsigned>(rxLen));
    if (rxLen == 0) {
        error = "i2c_driver_read_ddcci_auto returned no data";
        return false;
    }

    std::vector<uint8_t> rawData(readBuf.begin(), readBuf.begin() + static_cast<size_t>(rxLen));
    if (!ExtractFirstValidDDCReply(rawData, rxData, error)) {
        SPM_WriteLog(L"DDCSendRawI2CDev[auto-read]: invalid reply: %S raw=[%s]",
                     error.c_str(), BytesToHex(rawData).c_str());
        return false;
    }

    SPM_WriteLog(L"DDCSendRawI2CDev[auto-read]: read ok len=%zu raw=[%s]",
                 rxData.size(), BytesToHex(rxData).c_str());
    return true;
}

bool SerialPortManager::DDCSendRawI2CDevWriteOnly(const std::vector<uint8_t>& txPacket,
                                                  std::string& error) {
    intptr_t txLen = static_cast<intptr_t>(txPacket.size());
    SPM_WriteLog(L"DDCSendRawI2CDev[write-only]: write addr=0x%02X len=%lld data=[%s]",
                 DDC_7BIT_ADDR, static_cast<long long>(txLen), BytesToHex(txPacket).c_str());

    DWORD sehCode = 0;
    const bool writeOk = SafeI2CDriverWrite(g_i2cDevContext, DDC_7BIT_ADDR,
                                            const_cast<uint8_t*>(txPacket.data()), &txLen, &sehCode);
    if (sehCode != 0) {
        SPM_WriteLog(L"DDCSendRawI2CDev[write-only]: i2c_driver_write raised SEH 0x%08X", sehCode);
        error = "i2c_driver_write raised SEH";
        return false;
    }

    if (!writeOk) {
        const std::wstring lastError = GetI2CDevLastErrorText();
        SPM_WriteLog(L"DDCSendRawI2CDev[write-only]: i2c_driver_write failed, err=%s",
                     lastError.c_str());
        error = BuildI2CDevErrorMessage("i2c_driver_write failed");
        return false;
    }

    SPM_WriteLog(L"DDCSendRawI2CDev[write-only]: write ok, transferred=%lld",
                 static_cast<long long>(txLen));
    error.clear();
    return true;
}

bool SerialPortManager::DDCReadReplyI2CDevAfterWrite(std::vector<uint8_t>& rxData,
                                                     std::string& error) {
    rxData.clear();
    WaitBeforeRawRead(L"DDCReadReplyI2CDevAfterWrite");

    std::string readError;
    {
        std::vector<uint8_t> readBuf(512, 0);
        intptr_t rxLen = static_cast<intptr_t>(readBuf.size());
        SPM_WriteLog(L"DDCReadReplyI2CDevAfterWrite[read]: addr=0x%02X requestCapacity=%lld",
                     DDC_7BIT_ADDR, static_cast<long long>(rxLen));

        DWORD sehCode = 0;
        const bool readOk = SafeI2CDriverRead(g_i2cDevContext, DDC_7BIT_ADDR, readBuf.data(), &rxLen, &sehCode);
        if (sehCode != 0) {
            SPM_WriteLog(L"DDCReadReplyI2CDevAfterWrite[read]: i2c_driver_read raised SEH 0x%08X", sehCode);
            readError = "i2c_driver_read raised SEH";
        } else if (!readOk) {
            const std::wstring lastError = GetI2CDevLastErrorText();
            SPM_WriteLog(L"DDCReadReplyI2CDevAfterWrite[read]: i2c_driver_read failed, err=%s",
                         lastError.c_str());
            readError = BuildI2CDevErrorMessage("i2c_driver_read failed");
        } else if (rxLen <= 0) {
            SPM_WriteLog(L"DDCReadReplyI2CDevAfterWrite[read]: read returned no data");
            readError = "i2c_driver_read returned no data";
        } else {
            if (rxLen > static_cast<intptr_t>(readBuf.size())) {
                rxLen = static_cast<intptr_t>(readBuf.size());
            }
            std::vector<uint8_t> rawData(readBuf.begin(), readBuf.begin() + static_cast<size_t>(rxLen));
            if (ExtractFirstValidDDCReply(rawData, rxData, readError)) {
                SPM_WriteLog(L"DDCReadReplyI2CDevAfterWrite[read]: read ok len=%zu raw=[%s]",
                             rxData.size(), BytesToHex(rxData).c_str());
                error.clear();
                return true;
            }

            SPM_WriteLog(L"DDCReadReplyI2CDevAfterWrite[read]: invalid reply: %S raw=[%s]",
                         readError.c_str(), BytesToHex(rawData).c_str());
        }
    }

    if (g_i2c_driver_read_ddcci_auto) {
        std::vector<uint8_t> readBuf(255, 0);
        uint8_t rxLen = static_cast<uint8_t>(readBuf.size());
        SPM_WriteLog(L"DDCReadReplyI2CDevAfterWrite[auto-read]: addr=0x%02X requestCapacity=%u",
                     DDC_7BIT_ADDR, static_cast<unsigned>(rxLen));

        DWORD sehCode = 0;
        const bool autoReadOk = SafeI2CDriverReadDdcciAuto(g_i2cDevContext, DDC_7BIT_ADDR,
                                                           readBuf.data(), &rxLen, &sehCode);
        if (sehCode != 0) {
            SPM_WriteLog(L"DDCReadReplyI2CDevAfterWrite[auto-read]: i2c_driver_read_ddcci_auto raised SEH 0x%08X",
                         sehCode);
            error = readError + "; i2c_driver_read_ddcci_auto raised SEH";
            return false;
        }

        if (!autoReadOk) {
            const std::wstring lastError = GetI2CDevLastErrorText();
            SPM_WriteLog(L"DDCReadReplyI2CDevAfterWrite[auto-read]: i2c_driver_read_ddcci_auto failed, err=%s",
                         lastError.c_str());
            error = readError + "; " + BuildI2CDevErrorMessage("i2c_driver_read_ddcci_auto failed");
            return false;
        }

        if (rxLen == 0) {
            SPM_WriteLog(L"DDCReadReplyI2CDevAfterWrite[auto-read]: auto-read returned no data");
            error = readError + "; i2c_driver_read_ddcci_auto returned no data";
            return false;
        }

        std::vector<uint8_t> rawData(readBuf.begin(), readBuf.begin() + static_cast<size_t>(rxLen));
        if (ExtractFirstValidDDCReply(rawData, rxData, error)) {
            SPM_WriteLog(L"DDCReadReplyI2CDevAfterWrite[auto-read]: read ok len=%zu raw=[%s]",
                         rxData.size(), BytesToHex(rxData).c_str());
            return true;
        }

        SPM_WriteLog(L"DDCReadReplyI2CDevAfterWrite[auto-read]: invalid reply: %S raw=[%s]",
                     error.c_str(), BytesToHex(rawData).c_str());
        error = readError + "; auto-read failed: " + error;
        return false;
    }

    error = readError;
    return false;
}

bool SerialPortManager::DDCSendRawI2CDev(const std::vector<uint8_t>& txBody,
                                         std::vector<uint8_t>& rxData,
                                         std::string& error) {
    if (!IsOpen() || !g_i2cDevContext) {
        error = "i2c_dev backend not open";
        return false;
    }

    uint8_t lenByte = 0x80 | static_cast<uint8_t>(txBody.size());
    uint8_t checksum = DDC_WRITE_ADDR ^ DDC_HOST_ADDR ^ lenByte;
    for (uint8_t b : txBody) {
        checksum ^= b;
    }

    std::vector<uint8_t> txPacket;
    txPacket.reserve(txBody.size() + 3);
    txPacket.push_back(DDC_HOST_ADDR);
    txPacket.push_back(lenByte);
    txPacket.insert(txPacket.end(), txBody.begin(), txBody.end());
    txPacket.push_back(checksum);

    const std::wstring deviceName = StripI2CDevPrefix(m_deviceName);
    const bool isNvapiDevice = ContainsNoCase(deviceName, L"nvapi") ||
                               ContainsNoCase(deviceName, L"nvidia") ||
                               ContainsNoCase(deviceName, L"geforce");
    const bool isWriteOnlyBody = IsWriteOnlyPreferredRawBody(txBody);
    if (isWriteOnlyBody && isNvapiDevice) {
        SPM_WriteLog(L"DDCSendRawI2CDev: trying write-only path for NVAPI raw body on %s",
                     deviceName.c_str());
        if (DDCSendRawI2CDevWriteOnly(txPacket, error)) {
            std::string replyError;
            if (DDCReadReplyI2CDevAfterWrite(rxData, replyError)) {
                return true;
            }

            SPM_WriteLog(L"DDCSendRawI2CDev: NVAPI write succeeded but reply read failed for %s, treat as no-reply success. err=%S",
                         deviceName.c_str(), replyError.c_str());
            error.clear();
            rxData.clear();
            return true;
        }
        SPM_WriteLog(L"DDCSendRawI2CDev: write-only path failed for %s, fallback to split read path. err=%S",
                     deviceName.c_str(), error.c_str());
    }

    const bool preferAutoReadPath = IsAutoReadPreferredDevice();
    if (preferAutoReadPath) {
        SPM_WriteLog(L"DDCSendRawI2CDev: using auto-read preferred split path for %s", deviceName.c_str());
        std::string autoReadError;
        if (DDCSendRawI2CDevWriteThenAutoRead(txPacket, rxData, autoReadError)) {
            return true;
        }
        SPM_WriteLog(L"DDCSendRawI2CDev: auto-read path failed, fallback to write/read path. err=%S",
                     autoReadError.c_str());

        std::string writeReadError;
        if (DDCSendRawI2CDevWriteThenRead(txPacket, rxData, writeReadError)) {
            return true;
        }
        error = autoReadError + "; write/read fallback failed: " + writeReadError;
        return false;
    }

    return DDCSendRawI2CDevWriteThenRead(txPacket, rxData, error);
}

bool SerialPortManager::DDCSendRaw(const std::vector<uint8_t>& txBody,
                                   std::vector<uint8_t>& rxData,
                                   std::string& error) {
    return DDCSendRawI2CDev(txBody, rxData, error);
}

bool SerialPortManager::SendDDCPacket(const std::vector<uint8_t>& body) {
    std::vector<uint8_t> rxIgnore;
    std::string error;
    return DDCSendRawI2CDev(body, rxIgnore, error);
}

std::vector<uint8_t> SerialPortManager::ReceiveDDCPacket(size_t expectedLen) {
    (void)expectedLen;
    return {};
}
