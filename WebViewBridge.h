#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <wrl/client.h>
#include <wrl/event.h>
#include <string>
#include <future>
#include <mutex>
#include <WebView2.h>
#include "UpdateChecker.h"

class MonitorManager;

class WebViewBridge {
public:
    WebViewBridge(MonitorManager* monitorMgr);
    ~WebViewBridge();

    WebViewBridge(const WebViewBridge&) = delete;
    WebViewBridge& operator=(const WebViewBridge&) = delete;

    HRESULT Initialize(HWND hwnd);
    void Resize();
    void Close();
    void ExtractWebResources(HINSTANCE hInstance);

private:
    HRESULT OnEnvironmentCreated(HWND hwnd, HRESULT result, ICoreWebView2Environment* env);
    HRESULT OnControllerCreated(HRESULT result, ICoreWebView2Controller* controller);
    HRESULT OnWebMessageReceived(ICoreWebView2* sender, ICoreWebView2WebMessageReceivedEventArgs* args);
    std::wstring HandleRequest(const std::wstring& json);

    std::wstring BuildMonitorList();
    std::wstring BuildError(const std::wstring& msg);
    std::wstring BuildGetCapabilitiesResponse(int monitorIndex);
    std::wstring BuildGetVCPResponse(int monitorIndex, uint8_t vcpCode);
    std::wstring BuildSetVCPResponse(int monitorIndex, uint8_t vcpCode, uint32_t value);
    std::wstring BuildRawCommandResponse(int monitorIndex, const std::wstring& bodyHex);
    std::wstring EscapeJson(const std::wstring& s);

    std::wstring GetWebDirPath();

    // Update check handlers
    std::wstring HandleStartUpdateCheck();
    std::wstring HandlePollUpdateCheck();
    std::wstring HandleDownloadUpdate(const std::wstring& json);
    std::wstring HandleGetDownloadProgress();
    std::wstring HandleApplyUpdate();
    std::wstring HandleOpenUrl(const std::wstring& json);

    HWND m_hwnd = nullptr;
    MonitorManager* m_monitorMgr = nullptr;
    Microsoft::WRL::ComPtr<ICoreWebView2Controller> m_controller;
    Microsoft::WRL::ComPtr<ICoreWebView2> m_webview;
    EventRegistrationToken m_webMessageToken = {};
    EventRegistrationToken m_navCompleteToken = {};
    std::wstring m_webDir;
    std::wstring m_dataDir;

    // Update check async state
    std::future<UpdateInfo> m_checkFuture;
    std::mutex m_checkMutex;
    bool m_checkInProgress = false;
    UpdateInfo m_checkResult;

    // Download async state
    std::future<std::wstring> m_downloadFuture;
    std::mutex m_downloadMutex;
    double m_downloadProgress = 0.0;
    std::wstring m_pendingUpdatePath;
};
