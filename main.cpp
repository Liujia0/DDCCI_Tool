// Tell dxva2.h we're using the high-level monitor config API
#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS

#include <Windows.h>
#include "MonitorManager.h"
#include "WebViewBridge.h"
#include "resource.h"

static const wchar_t* WINDOW_CLASS = L"DDCCI_Tool_Window";
static const wchar_t* WINDOW_TITLE = L"DDCCI Monitor Tool";
static const int WINDOW_WIDTH  = 960;
static const int WINDOW_HEIGHT = 680;

struct AppState {
    MonitorManager monitorMgr;
    WebViewBridge bridge;

    AppState() : bridge(&monitorMgr) {}
};

static AppState* g_app = nullptr;

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        g_app = new AppState();

        g_app->monitorMgr.EnumerateMonitors();

        if (FAILED(g_app->bridge.Initialize(hwnd))) {
            MessageBoxW(hwnd,
                L"Failed to initialize WebView2. Please ensure the WebView2 Runtime is installed.\n\n"
                L"Download: https://go.microsoft.com/fwlink/p/?LinkId=2124703",
                L"DDCCI Tool Error", MB_ICONERROR);
            return -1;
        }
        return 0;
    }

    case WM_SIZE: {
        if (g_app && wParam != SIZE_MINIMIZED) {
            g_app->bridge.Resize();
        }
        return 0;
    }

    case WM_DESTROY:
        if (g_app) {
            g_app->bridge.Close();
            delete g_app;
            g_app = nullptr;
        }
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(WNDCLASSEXW);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hIcon         = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON));
    wc.hIconSm       = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON));
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(0x1E, 0x1E, 0x2E));
    wc.lpszClassName = WINDOW_CLASS;

    if (!RegisterClassExW(&wc)) {
        MessageBoxW(nullptr, L"Window registration failed.", L"Error", MB_ICONERROR);
        return 1;
    }

    HWND hwnd = CreateWindowExW(
        0, WINDOW_CLASS, WINDOW_TITLE, WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, WINDOW_WIDTH, WINDOW_HEIGHT,
        nullptr, nullptr, hInstance, nullptr);

    if (!hwnd) {
        MessageBoxW(nullptr, L"Window creation failed.", L"Error", MB_ICONERROR);
        return 1;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    CoUninitialize();
    return static_cast<int>(msg.wParam);
}
