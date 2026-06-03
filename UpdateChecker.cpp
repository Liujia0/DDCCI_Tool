#include "UpdateChecker.h"
#include <winhttp.h>
#include <shellapi.h>
#include <cwctype>
#include <sstream>
#include <vector>
#include <algorithm>
#include <fstream>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "version.lib")
#pragma comment(lib, "shell32.lib")

// ---------------------------------------------------------------------------
// UTF-8 <-> wstring conversion
// ---------------------------------------------------------------------------

static std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
                                  nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string out(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
                        &out[0], len, nullptr, nullptr);
    return out;
}

static std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(),
                                  nullptr, 0);
    if (len <= 0) return {};
    std::wstring out(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(),
                        &out[0], len);
    return out;
}

// ---------------------------------------------------------------------------
// GetCurrentVersion — read VS_FIXEDFILEINFO from own exe
// ---------------------------------------------------------------------------

std::wstring UpdateChecker::GetCurrentVersion() {
    WCHAR path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);

    DWORD dummy = 0;
    DWORD size = GetFileVersionInfoSizeW(path, &dummy);
    if (size == 0) return L"0.0.0";

    std::vector<BYTE> buf(size);
    if (!GetFileVersionInfoW(path, 0, size, buf.data()))
        return L"0.0.0";

    VS_FIXEDFILEINFO* info = nullptr;
    UINT infoLen = 0;
    if (!VerQueryValueW(buf.data(), L"\\", reinterpret_cast<LPVOID*>(&info), &infoLen))
        return L"0.0.0";

    int major = HIWORD(info->dwFileVersionMS);
    int minor = LOWORD(info->dwFileVersionMS);
    int build = HIWORD(info->dwFileVersionLS);

    std::wostringstream ss;
    ss << major << L"." << minor << L"." << build;
    return ss.str();
}

// ---------------------------------------------------------------------------
// Version comparison
// ---------------------------------------------------------------------------

std::wstring UpdateChecker::NormalizeVersion(const std::wstring& tag) {
    std::wstring v = tag;
    if (!v.empty() && (v[0] == L'v' || v[0] == L'V'))
        v = v.substr(1);
    return v;
}

void UpdateChecker::ParseTriple(const std::wstring& v, int out[3]) {
    out[0] = out[1] = out[2] = 0;
    std::wstring seg;
    int idx = 0;
    for (size_t i = 0; i < v.size() && idx < 3; i++) {
        wchar_t ch = v[i];
        if (ch == L'.' || ch == L'-' || ch == L'+') {
            if (!seg.empty()) {
                out[idx++] = std::stoi(seg);
                seg.clear();
            }
            if (ch == L'-' || ch == L'+') break; // pre-release/build metadata
        } else if (ch >= L'0' && ch <= L'9') {
            seg += ch;
        }
    }
    if (!seg.empty() && idx < 3)
        out[idx] = std::stoi(seg);
}

bool UpdateChecker::IsNewer(const std::wstring& latest, const std::wstring& current) {
    int l[3], c[3];
    ParseTriple(NormalizeVersion(latest), l);
    ParseTriple(NormalizeVersion(current), c);
    for (int i = 0; i < 3; i++) {
        if (l[i] > c[i]) return true;
        if (l[i] < c[i]) return false;
    }
    return false;
}

// ---------------------------------------------------------------------------
// WinHTTP helpers — URL parsing
// ---------------------------------------------------------------------------

struct UrlParts {
    std::wstring host;
    std::wstring path;
    bool https = true;
};

static UrlParts ParseUrl(const std::wstring& url) {
    UrlParts parts;
    // Find scheme
    size_t schemeEnd = url.find(L"://");
    if (schemeEnd == std::wstring::npos) return parts;

    std::wstring scheme = url.substr(0, schemeEnd);
    parts.https = (scheme == L"https");

    size_t hostStart = schemeEnd + 3;
    size_t pathStart = url.find(L'/', hostStart);
    if (pathStart == std::wstring::npos) {
        parts.host = url.substr(hostStart);
        parts.path = L"/";
    } else {
        parts.host = url.substr(hostStart, pathStart - hostStart);
        parts.path = url.substr(pathStart);
    }
    return parts;
}

// ---------------------------------------------------------------------------
// HttpGet — WinHTTP GET request, returns response body as wstring
// ---------------------------------------------------------------------------

std::wstring UpdateChecker::HttpGet(const std::wstring& url, DWORD timeoutMs) {
    UrlParts parts = ParseUrl(url);
    if (parts.host.empty()) return {};

    HINTERNET hSession = WinHttpOpen(USER_AGENT,
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return {};

    WinHttpSetTimeouts(hSession, timeoutMs, timeoutMs, timeoutMs, timeoutMs);

    HINTERNET hConnect = WinHttpConnect(hSession, parts.host.c_str(),
        parts.https ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return {}; }

    DWORD flags = parts.https ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", parts.path.c_str(),
        L"HTTP/1.1", WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return {}; }

    // TLS 1.2 + 1.3
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
    protocols |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
#endif
    WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));

    // Add Accept header
    WinHttpAddRequestHeaders(hRequest, L"Accept: application/vnd.github+json",
        (ULONG)-1, WINHTTP_ADDREQ_FLAG_ADD);

    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(hRequest, nullptr)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return {};
    }

    // Read response body — use QueryDataAvailable for reliable chunked reads
    std::string body;
    char buf[8192];
    DWORD bytesAvailable = 0;
    while (WinHttpQueryDataAvailable(hRequest, &bytesAvailable) && bytesAvailable > 0) {
        DWORD toRead = (bytesAvailable > sizeof(buf)) ? sizeof(buf) : bytesAvailable;
        DWORD bytesRead = 0;
        if (!WinHttpReadData(hRequest, buf, toRead, &bytesRead) || bytesRead == 0)
            break;
        body.append(buf, bytesRead);
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    return Utf8ToWide(body);
}

// ---------------------------------------------------------------------------
// Simple JSON parsing (string search, no third-party lib)
// ---------------------------------------------------------------------------

std::wstring UpdateChecker::ParseJsonString(const std::wstring& json, const std::wstring& key) {
    std::wstring search = L"\"" + key + L"\"";
    size_t pos = json.find(search);
    if (pos == std::wstring::npos) return {};

    pos += search.size();
    // Skip whitespace and colon
    while (pos < json.size() && (json[pos] == L' ' || json[pos] == L'\t' ||
           json[pos] == L'\r' || json[pos] == L'\n' || json[pos] == L':'))
        pos++;

    if (pos >= json.size() || json[pos] != L'"') return {};
    pos++; // skip opening quote

    std::wstring result;
    while (pos < json.size()) {
        wchar_t ch = json[pos];
        if (ch == L'\\' && pos + 1 < json.size()) {
            wchar_t next = json[pos + 1];
            switch (next) {
                case L'"':  result += L'"';  break;
                case L'\\': result += L'\\'; break;
                case L'/':  result += L'/';  break;
                case L'n':  result += L'\n'; break;
                case L'r':  result += L'\r'; break;
                case L't':  result += L'\t'; break;
                default:    result += L'\\'; result += next; break;
            }
            pos += 2;
        } else if (ch == L'"') {
            break; // end of string
        } else {
            result += ch;
            pos++;
        }
    }
    return result;
}

bool UpdateChecker::ParseAssetInfo(const std::wstring& json, UpdateInfo& info) {
    // Find "assets":[
    size_t assetsPos = json.find(L"\"assets\"");
    if (assetsPos == std::wstring::npos) return false;

    size_t arrStart = json.find(L'[', assetsPos);
    if (arrStart == std::wstring::npos) return false;

    // Find the matching ]
    int depth = 1;
    size_t arrEnd = arrStart + 1;
    while (arrEnd < json.size() && depth > 0) {
        if (json[arrEnd] == L'[') depth++;
        else if (json[arrEnd] == L']') depth--;
        if (depth > 0) arrEnd++;
    }

    std::wstring assetsStr = json.substr(arrStart, arrEnd - arrStart + 1);

    // Search for matching asset name within the assets array
    std::wstring nameSearch = L"\"name\"";
    size_t searchPos = 0;
    while (searchPos < assetsStr.size()) {
        size_t namePos = assetsStr.find(nameSearch, searchPos);
        if (namePos == std::wstring::npos) break;

        // Extract the name value
        size_t colonPos = assetsStr.find(L':', namePos + nameSearch.size());
        if (colonPos == std::wstring::npos) break;

        size_t quoteStart = assetsStr.find(L'"', colonPos + 1);
        if (quoteStart == std::wstring::npos) break;

        size_t quoteEnd = assetsStr.find(L'"', quoteStart + 1);
        if (quoteEnd == std::wstring::npos) break;

        std::wstring assetName = assetsStr.substr(quoteStart + 1, quoteEnd - quoteStart - 1);

        if (assetName == ASSET_NAME) {
            // Find the enclosing { ... } for this asset
            size_t braceStart = assetsStr.rfind(L'{', namePos);
            if (braceStart == std::wstring::npos) { searchPos = quoteEnd + 1; continue; }

            int braceDepth = 1;
            size_t braceEnd = braceStart + 1;
            while (braceEnd < assetsStr.size() && braceDepth > 0) {
                if (assetsStr[braceEnd] == L'{') braceDepth++;
                else if (assetsStr[braceEnd] == L'}') braceDepth--;
                if (braceDepth > 0) braceEnd++;
            }

            std::wstring assetBlock = assetsStr.substr(braceStart, braceEnd - braceStart + 1);

            // Extract browser_download_url
            info.downloadUrl = ParseJsonString(assetBlock, L"browser_download_url");
            info.assetName = assetName;

            // Extract size
            std::wstring sizeSearch = L"\"size\"";
            size_t sizePos = assetBlock.find(sizeSearch);
            if (sizePos != std::wstring::npos) {
                size_t sizeColon = assetBlock.find(L':', sizePos + sizeSearch.size());
                if (sizeColon != std::wstring::npos) {
                    size_t numStart = sizeColon + 1;
                    while (numStart < assetBlock.size() && (assetBlock[numStart] == L' ' || assetBlock[numStart] == L'\t'))
                        numStart++;
                    size_t numEnd = numStart;
                    while (numEnd < assetBlock.size() && assetBlock[numEnd] >= L'0' && assetBlock[numEnd] <= L'9')
                        numEnd++;
                    if (numEnd > numStart) {
                        info.assetSize = std::stoll(assetBlock.substr(numStart, numEnd - numStart));
                    }
                }
            }
            return true;
        }

        searchPos = quoteEnd + 1;
    }
    return false;
}

// ---------------------------------------------------------------------------
// CheckForUpdate
// ---------------------------------------------------------------------------

UpdateInfo UpdateChecker::CheckForUpdate() {
    UpdateInfo info;
    info.currentVersion = GetCurrentVersion();

    std::wstring json = HttpGet(GITHUB_API);
    if (json.empty()) {
        info.error = L"Failed to connect to GitHub API";
        return info;
    }

    // Check for API error — GitHub returns {"message": "..."} on errors
    std::wstring message = ParseJsonString(json, L"message");
    if (!message.empty()) {
        // Friendly message for rate limit
        if (message.find(L"rate limit") != std::wstring::npos) {
            info.error = L"GitHub API rate limit exceeded. Please try again later.";
        } else {
            // Truncate other long error messages
            if (message.size() > 120) message = message.substr(0, 120);
            info.error = message;
        }
        return info;
    }

    info.latestVersion = NormalizeVersion(ParseJsonString(json, L"tag_name"));
    info.releaseUrl = ParseJsonString(json, L"html_url");
    info.releaseNotes = ParseJsonString(json, L"body");

    if (info.latestVersion.empty()) {
        info.error = L"Could not parse version from release";
        return info;
    }

    ParseAssetInfo(json, info);

    info.hasUpdate = IsNewer(info.latestVersion, info.currentVersion);
    return info;
}

// ---------------------------------------------------------------------------
// DownloadUpdate — WinHTTP streaming download with progress
// ---------------------------------------------------------------------------

std::wstring UpdateChecker::DownloadUpdate(const std::wstring& url,
    std::function<void(double)> progress)
{
    if (url.empty()) return {};

    // Determine output path: same dir as exe, filename = DDCCI_Tool.exe.new
    WCHAR exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring exeDir(exePath);
    size_t lastSlash = exeDir.find_last_of(L"\\/");
    if (lastSlash != std::wstring::npos)
        exeDir = exeDir.substr(0, lastSlash);
    std::wstring outPath = exeDir + L"\\DDCCI_Tool.exe.new";

    UrlParts parts = ParseUrl(url);
    if (parts.host.empty()) return {};

    HINTERNET hSession = WinHttpOpen(USER_AGENT,
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return {};

    // 5 minute timeout for downloads
    DWORD timeoutMs = 300000;
    WinHttpSetTimeouts(hSession, timeoutMs, timeoutMs, timeoutMs, timeoutMs);

    HINTERNET hConnect = WinHttpConnect(hSession, parts.host.c_str(),
        parts.https ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return {}; }

    DWORD flags = parts.https ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", parts.path.c_str(),
        L"HTTP/1.1", WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return {}; }

    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
    protocols |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
#endif
    WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));

    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(hRequest, nullptr)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return {};
    }

    // Query Content-Length for progress
    DWORD contentLen = 0;
    DWORD bufLen = sizeof(contentLen);
    WinHttpQueryHeaders(hRequest,
        WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &contentLen, &bufLen, WINHTTP_NO_HEADER_INDEX);

    // Open output file
    std::ofstream outFile(outPath, std::ios::binary);
    if (!outFile.is_open()) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return {};
    }

    char buf[81920]; // 80KB buffer
    DWORD totalRead = 0;
    DWORD bytesAvailable = 0;

    while (WinHttpQueryDataAvailable(hRequest, &bytesAvailable) && bytesAvailable > 0) {
        DWORD toRead = (bytesAvailable > sizeof(buf)) ? sizeof(buf) : bytesAvailable;
        DWORD bytesRead = 0;
        if (!WinHttpReadData(hRequest, buf, toRead, &bytesRead) || bytesRead == 0)
            break;
        outFile.write(buf, bytesRead);
        totalRead += bytesRead;

        if (progress && contentLen > 0) {
            progress(static_cast<double>(totalRead) / static_cast<double>(contentLen));
        }
    }

    outFile.close();
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    if (progress) progress(1.0);
    return outPath;
}

// ---------------------------------------------------------------------------
// ApplyAndRestart — generate bat script, launch it, exit
// ---------------------------------------------------------------------------

bool UpdateChecker::ApplyAndRestart(const std::wstring& newExePath) {
    WCHAR exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring currentExe(exePath);

    // Build bat script
    std::wostringstream bat;
    bat << L"@echo off\r\n"
        << L"setlocal\r\n"
        << L"set RETRIES=40\r\n"
        << L":waitloop\r\n"
        << L"timeout /t 1 /nobreak >nul\r\n"
        << L"del \"" << currentExe << L"\" >nul 2>&1\r\n"
        << L"if exist \"" << currentExe << L"\" (\r\n"
        << L"  set /a RETRIES=%RETRIES%-1\r\n"
        << L"  if %RETRIES% gtr 0 goto waitloop\r\n"
        << L"  exit /b 1\r\n"
        << L")\r\n"
        << L"move /y \"" << newExePath << L"\" \"" << currentExe << L"\" >nul\r\n"
        << L"start \"\" \"" << currentExe << L"\"\r\n"
        << L"(goto) 2>nul & del \"%~f0\"\r\n";

    // Write to %TEMP%\ddcci_update_XXX.bat
    WCHAR tempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    std::wstring batPath = std::wstring(tempPath) + L"ddcci_update_" +
        std::to_wstring(GetCurrentProcessId()) + L".bat";

    {
        std::ofstream batFile(batPath, std::ios::binary);
        if (!batFile.is_open()) return false;
        // Write as system ANSI codepage (not UTF-8) — cmd.exe requires it
        int len = WideCharToMultiByte(CP_ACP, 0, bat.str().c_str(), (int)bat.str().size(),
                                      nullptr, 0, nullptr, nullptr);
        if (len > 0) {
            std::string ansi(len, '\0');
            WideCharToMultiByte(CP_ACP, 0, bat.str().c_str(), (int)bat.str().size(),
                                &ansi[0], len, nullptr, nullptr);
            batFile.write(ansi.c_str(), ansi.size());
        }
    }

    // Launch bat with cmd.exe, hidden window
    std::wstring cmdLine = L"cmd.exe /c \"" + batPath + L"\"";

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};

    BOOL ok = CreateProcessW(nullptr, &cmdLine[0], nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);

    if (ok) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    return ok != FALSE;
}

// ---------------------------------------------------------------------------
// OpenUrl
// ---------------------------------------------------------------------------

void UpdateChecker::OpenUrl(const std::wstring& url) {
    // Only allow http/https schemes to prevent shell injection
    if (url.size() < 8) return;
    std::wstring scheme = url.substr(0, 8);
    for (auto& ch : scheme) ch = towlower(ch);
    if (scheme.substr(0, 7) != L"http://" && scheme.substr(0, 8) != L"https://") return;
    ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOW);
}
