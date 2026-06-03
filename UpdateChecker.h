#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <string>
#include <functional>

struct UpdateInfo {
    bool hasUpdate = false;
    std::wstring currentVersion;
    std::wstring latestVersion;
    std::wstring releaseUrl;
    std::wstring downloadUrl;
    std::wstring assetName;
    long long assetSize = 0;
    std::wstring releaseNotes;
    std::wstring error;
};

class UpdateChecker {
public:
    // 获取当前版本（从 exe 的 VS_VERSION_INFO 读取）
    static std::wstring GetCurrentVersion();

    // 同步检查更新（应在后台线程调用）
    static UpdateInfo CheckForUpdate();

    // 下载更新到 exe 同目录的 DDCCI_Tool.exe.new，progress 回调 0.0~1.0
    // 返回下载文件路径，失败返回空
    static std::wstring DownloadUpdate(const std::wstring& url,
        std::function<void(double)> progress);

    // 生成 bat 脚本替换 exe 并重启
    static bool ApplyAndRestart(const std::wstring& newExePath);

    // 用默认浏览器打开 URL
    static void OpenUrl(const std::wstring& url);

private:
    static constexpr const wchar_t* GITHUB_API =
        L"https://api.github.com/repos/Liujia0/DDCCI_Tool-releases/releases/latest";
    static constexpr const wchar_t* USER_AGENT = L"DDCCI_Tool-UpdateChecker";
    static constexpr const wchar_t* ASSET_NAME = L"DDCCI_Tool.exe";

    static bool IsNewer(const std::wstring& latest, const std::wstring& current);
    static std::wstring NormalizeVersion(const std::wstring& tag);
    static void ParseTriple(const std::wstring& v, int out[3]);

    // WinHTTP helpers
    static std::wstring HttpGet(const std::wstring& url, DWORD timeoutMs = 10000);
    static std::wstring ParseJsonString(const std::wstring& json, const std::wstring& key);
    static bool ParseAssetInfo(const std::wstring& json, UpdateInfo& info);
};
