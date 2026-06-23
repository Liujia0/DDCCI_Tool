#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <cstdint>
#include <string>
#include <vector>

struct SerialPortInfo {
    std::wstring portName;
    std::wstring description;
};

class SerialPortManager {
public:
    SerialPortManager() = default;
    ~SerialPortManager();

    SerialPortManager(const SerialPortManager&) = delete;
    SerialPortManager& operator=(const SerialPortManager&) = delete;

    static bool LoadI2CDev(const std::wstring& dllPath);
    static bool IsI2CDevLoaded();

    std::vector<SerialPortInfo> EnumeratePorts();
    bool OpenPort(const std::wstring& portName, DWORD baudRate = 9600);
    void ClosePort();
    bool IsOpen() const;
    std::wstring GetOpenPortName() const;

    bool SendDDCPacket(const std::vector<uint8_t>& body);
    std::vector<uint8_t> ReceiveDDCPacket(size_t expectedLen);
    bool DDCSendRaw(const std::vector<uint8_t>& txBody, std::vector<uint8_t>& rxData, std::string& error);

private:
    bool EnsureI2CDevReady();
    std::vector<SerialPortInfo> EnumerateI2CDevPorts();
    bool OpenI2CDevPort(const std::wstring& portName, DWORD baudRate);
    bool DDCSendRawI2CDev(const std::vector<uint8_t>& txBody, std::vector<uint8_t>& rxData, std::string& error);
    bool DDCSendRawI2CDevWriteReadRestart(const std::vector<uint8_t>& txPacket, std::vector<uint8_t>& rxData, std::string& error);
    bool DDCSendRawI2CDevWriteThenRead(const std::vector<uint8_t>& txPacket, std::vector<uint8_t>& rxData, std::string& error);
    bool DDCSendRawI2CDevWriteThenAutoRead(const std::vector<uint8_t>& txPacket, std::vector<uint8_t>& rxData, std::string& error);
    bool DDCSendRawI2CDevWriteOnly(const std::vector<uint8_t>& txPacket, std::string& error);
    bool DDCReadReplyI2CDevAfterWrite(std::vector<uint8_t>& rxData, std::string& error);
    bool ExtractFirstValidDDCReply(const std::vector<uint8_t>& rawData, std::vector<uint8_t>& packet, std::string& reason) const;
    bool IsValidDDCReply(const std::vector<uint8_t>& packet, std::string& reason) const;
    bool IsRestartPreferredDevice() const;
    bool IsAutoReadPreferredDevice() const;

    std::wstring m_deviceName;
};
