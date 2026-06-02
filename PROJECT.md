# DDCCI Tool — Project Overview

## Summary

Windows desktop application for reading/writing DDC/CI monitor settings (brightness, contrast, input source, color gains, etc.) with a WebView2-based dark-themed UI.

- **Platform**: Windows (x64)
- **Language**: C++17 + HTML/CSS/JS
- **Build**: Visual Studio 2022 (Community), MSBuild
- **UI Runtime**: Microsoft Edge WebView2 (NuGet package)
- **Total source**: ~2440 lines across 9 files

---

## Project Structure

```
DDCCI_Tool/
├── DDCCI_Tool.sln              # VS2022 solution
├── DDCCI_Tool.vcxproj          # MSBuild project
├── packages.config             # NuGet: Microsoft.Web.WebView2 1.0.2903.40
├── resource.h                  # RC resource IDs
├── DDCCI_Tool.rc               # Executable metadata (icon, version)
├── app.ico                     # App icon (6 sizes 16–256, PNG-in-ICO)
├── tools/
│   └── make_icon.py            # Regenerates app.ico from the logo design
├── main.cpp                    # WinMain entry point, window class, message loop     (98 lines)
├── MonitorManager.h            # Monitor info structs, manager class declaration     (46 lines)
├── MonitorManager.cpp          # DDC/CI API wrapper (dxva2)                          (272 lines)
├── WebViewBridge.h             # WebView2 host + JSON bridge declaration             (46 lines)
├── WebViewBridge.cpp           # WebView2 init, JS bridge, DDC/CI packet hex         (618 lines)
└── web/
    ├── index.html              # Main page: sidebar + tabs (Controls/Log/Raw)        (96 lines)
    ├── style.css               # Dark theme CSS                                      (665 lines)
    ├── logo.svg                # App logo (monitor + brightness motif, accent blue)
    ├── mccs.js                 # MCCS 2.2a VCP definition table (152 codes)          (438 lines)
    └── app.js                  # Frontend logic, bridge, logging                     (595 lines)
```

Output: `bin/x64/Debug/DDCCI_Tool.exe` + `web/` + `WebView2Loader.dll`

---

## Architecture

```
┌─────────────────────────────────────────────────┐
│  Win32 Desktop App (DDCCI_Tool.exe)             │
│  ┌──────────────────┐  ┌────────────────────┐   │
│  │  WebView2        │  │  MonitorManager    │   │
│  │  (Edge Chromium) │  │  (dxva2.lib)       │   │
│  │                  │  │                    │   │
│  │  HTML/CSS/JS  ◄──┤  │  EnumerateMonitors │   │
│  │  (Dark UI)       │  │  Get/SetVCP        │   │
│  │                  │  │  Capabilities      │   │
│  └──────────────────┘  └────────────────────┘   │
└─────────────────────────────────────────────────┘
```

### Bridge Protocol

JS sends JSON requests via `chrome.webview.postMessage()`. C++ dispatches and responds via `ExecuteScript()` calling `window.__bridgeReceive(json)` (not `PostWebMessageAsJson`, which proved unreliable for triggering the JS `message` event).

**Request (JS → C++):**
```json
{"method": "enumerateMonitors"}
{"method": "getCapabilities", "monitor": 0}
{"method": "getVCP", "monitor": 0, "vcpCode": 16}
{"method": "setVCP", "monitor": 0, "vcpCode": 16, "value": 50}
{"method": "sendRaw", "monitor": 0, "bodyHex": "01 10"}
```

**Response (C++ → JS via ExecuteScript):**
```json
{"type": "monitorList", "monitors": [{"index": 0, "name": "DELL U2723QE"}]}
{"type": "vcpFeature", "monitor": 0, "vcpCode": 16, "current": 50, "max": 100, "sendHex": "...", "recvHex": "..."}
{"type": "capabilities", "monitor": 0, "capabilities": "...", "supportedVCP": [...], "segments": [...], "sendHex": "...", "recvHex": "..."}
{"type": "rawResponse", "monitor": 0, "txHex": "...", "rxHex": "...", "parsed": "..."}
```

---

## Key Classes & Files

### MonitorManager (MonitorManager.h/.cpp)

Wraps Windows DDC/CI API (`dxva2.lib`, `physicalmonitorenumerationapi.h`, `lowlevelmonitorconfigurationapi.h`).

| Method | Purpose |
|--------|---------|
| `EnumerateMonitors()` | `EnumDisplayMonitors` → `GetPhysicalMonitorsFromHMONITOR`, extracts `model(xxx)` from DDC/CI capabilities for correct monitor names |
| `GetMonitorCount()` | Returns number of detected physical monitors |
| `GetMonitor(i)` | Returns `MonitorInfo*` with `hPhysicalMonitor` + `name` |
| `GetCapabilities(i)` | Calls `GetCapabilitiesStringLength` + `CapabilitiesRequestAndCapabilitiesReply`, returns wide-string capabilities |
| `GetCapabilitiesStringLen(i)` | Just queries expected length (used for segment estimation when read fails) |
| `GetSupportedVCPCodes(i)` | Parses `vcp(10 12 16 ...)` from capabilities string, returns list of supported VCP codes |
| `GetVCPFeature(i, vcpCode)` | Calls `GetVCPFeatureAndVCPFeatureReply`, returns `{current, max, valid}` (`valid=false` on read failure) |
| `SetVCP(i, vcpCode, value)` | Calls `SetVCPFeature` |

**Monitor naming**: `szPhysicalMonitorDescription` always returns "Generic PnP Monitor". The only reliable way to get real model names is extracting `model(CU34P2C)` from the DDC/CI capabilities string — this is done during enumeration in `MonitorEnumProc`.

**Diagnostic logging**: In `_DEBUG` builds only, writes to `debug.log` in the EXE directory (compiled out of Release). Captures enumeration progress, physical monitor counts, capabilities read results, and error codes.

### WebViewBridge (WebViewBridge.h/.cpp)

WebView2 host + JSON bridge + DDC/CI packet hex computation.

| Method | Purpose |
|--------|---------|
| `Initialize(HWND)` | Creates WebView2 environment + controller, registers `WebMessageReceived` handler |
| `HandleRequest(json)` | Dispatches by `method` field: `enumerateMonitors`, `getCapabilities`, `getVCP`, `setVCP`, `sendRaw` |
| `BuildMonitorList()` | JSON response with all monitor names |
| `BuildGetCapabilitiesResponse(i)` | JSON with caps string, supported VCP codes, segmented send/recv hex |
| `BuildGetVCPResponse(i, vcp)` | JSON with current/max values + computed send/recv hex |
| `BuildSetVCPResponse(i, vcp, val)` | JSON after set + computed send/recv hex |
| `BuildRawCommandResponse(i, bodyHex)` | Parses body hex, dispatches to GetVCP/SetVCP/Capabilities, returns raw hex response |

**DDCI packet hex computations** (anonymous namespace):
- `BuildVCPGetSendHex(vcpCode)` — `6E 51 82 01 [VCP] [CHK]`
- `BuildVCPGetRecvHex(vcpCode, cur, max)` — `6E 51 87 00 [VCP] 00 [MAX_HI] [MAX_LO] [CUR_HI] [CUR_LO] [CHK]`
- `BuildVCPSetSendHex(vcpCode, value)` — `6E 51 84 03 [VCP] [VAL_HI] [VAL_LO] [CHK]`
- `BuildVCPSetRecvHex()` — `6E 51 81 00 [CHK]`
- `BuildCapsSendHex(offset=0)` — `6E 51 83 F3 [OFF_HI] [OFF_LO] [CHK]`
- `BuildCapsSegRecvHex(offset, nextOffset, chunk)` — per-segment: `6E 51 [LEN] 00 [NEXT_HI] [NEXT_LO] [data...] [CHK]`
- `BuildCapsSegmentsJson(capsStr, totalLen)` — splits capabilities into 26-byte chunks, generates segment JSON; when capsStr is empty but totalLen > 0 (read failed), still generates estimated segments with empty recvHex

### main.cpp

Standard Win32 entry point:
1. `WinMain` → registers window class (dark background brush `#1e1e2e`)
2. Creates window at 960×680
3. Instantiates `MonitorManager` + `WebViewBridge`
4. `WM_CREATE` → `bridge.Initialize(hwnd)`
5. `WM_SIZE` → `bridge.Resize()`
6. `WM_DESTROY` → `bridge.Close()`, `PostQuitMessage`

### web/ (Frontend)

#### index.html
- Sidebar: monitor list + refresh button + status bar
- Main panel: Controls tab (VCP sliders + capabilities viewer), Log tab, Raw command tab

#### app.js (IIFE pattern)
- **MCCS-driven controls**: control definitions come entirely from `web/mccs.js`
  (`window.MCCS` — 152 VCP codes, grouped). No hardcoded feature list.
- **Capabilities-first flow**: `selectMonitor()` → `getCapabilities` → on response,
  `parseCapsVCP()` parses the raw `vcp(...)` list (including per-code sub-values, e.g.
  `14(05 0B)`), `buildVCPControls()` renders one control per supported, MCCS-defined code,
  grouped by MCCS functional category; `queryAllVCPFeatures()` reads every readable code.
- **Type-appropriate widgets** (by MCCS `type`/`access`): Continuous (and value-less NC
  byte ranges like audio volume) → slider; enumerated NC → `<select>` of named values
  (options filtered to the caps sub-value list when present, else all MCCS values); RO /
  Table → read-only value display (named for NC); WO → action button(s) (e.g. "Restore
  Factory Defaults", Save/Restore "Store"/"Restore"). Codes the monitor reports but MCCS
  does not define (manufacturer-specific) are **not** shown in Controls.
- **Log system**: `logRecords[]` array (max 500), `addLogEntry(dir, op, detail, sendHex, recvHex)`, `renderLogEntries()` renders log with TX/RX hex rows. VCP names come from the MCCS table (`vcpCodeName()`). Per-segment capabilities logging with `<read failed>` fallback
- **Raw command tab**: hex input with real-time TX packet preview (`computeTxHex()` auto-calculates `0x80|len` + CHK), quick command presets (`01 10`, `F3 00 00` etc.), response display with TX/RX/parsed info
- **Tab switching**: Controls | Log | Raw
- **Bridge**: `window.__bridgeReceive(data)` called by C++ via ExecuteScript, dispatches to `dispatchResponse()`

#### mccs.js
`window.MCCS = { groups: [...], vcp: {...} }`. The `groups` array orders the 8 MCCS
functional categories (Preset / Image / Color / Display / Geometry / Misc / Audio / DPVL).
Each `vcp[code]` has `{ name, group, access:'rw'|'ro'|'wo', type:'C'|'NC'|'T', values?, action? }`.
`values` maps value → label for enumerable NC codes (e.g. Input Source 0x60, Color Preset
0x14, Power Mode 0xD6, OSD Language 0xCC). `action:true` marks WO trigger codes
(restore-defaults family, degauss). Generated from `SPEC/MCCS 2.2a.pdf` Section 8 tables.

#### style.css
Dark theme using CSS variables. Key colors: `--bg-primary: #1e1e2e`, `--accent: #5b8df0`, `--sidebar-width: 220px`. Scrollbar styling, slider thumb styling, monospace log entries.

---

## DDC/CI Protocol Notes

### Packet Format
```
[6E] [51] [LEN] [DATA...] [CHK]
```
- `6E 51` = host source address + command byte
- `LEN` = `0x80 | (data_byte_count)` — counts the bytes after LEN (including the opcode byte)
- `CHK` = XOR of all preceding bytes (including `6E 51`)

### Capabilities Offset
The capabilities request uses a **2-byte offset** (not 3). Correct format:
- Request: `6E 51 83 F3 [OFF_HI] [OFF_LO] [CHK]` (LEN=0x83 = 3 data bytes: F3 + 2 offset bytes)
- Response payload: `[RESULT=0x00] [NEXT_OFF_HI] [NEXT_OFF_LO] [chunk_data...]`

### Capabilities String Format
```
(prot(monitor)type(LCD)model(DELL U2723QE)cmds(01 02 03 07 0C F3)vcp(02 04 05 08 10 12 14(05 06 08 0B) 16 18 1A ...)mccs_ver(2.2)...)
```
- `model(xxx)` — used to extract real monitor name
- `vcp(xx xx ...)` — hex VCP codes the monitor supports; `14(05 06 08 0B)` means code 0x14 with sub-parameter values

### Segmentation
Capabilities strings that don't fit in one I2C packet (~26 bytes of data per segment) are read in multiple segments. Each segment has its own send (with increasing offset) and receive (with result + next offset + chunk data).

---

## Known Issues & Gotchas

1. **`PostWebMessageAsJson` unreliable** — this API sometimes fails to trigger JS `message` events. Workaround: C++ uses `ExecuteScript` to call `window.__bridgeReceive(json)` directly.

2. **JS race condition** — the `message` event listener must be registered only after `chrome.webview` is available. The `init()` function polls with 50ms retries.

3. **`CapabilitiesRequestAndCapabilitiesReply` buffer size** — must pass the exact value returned by `GetCapabilitiesStringLength` (which includes the null terminator). Passing `buf.size()` (which is `len+1`) may cause failures on some monitors.

4. **Monitor names all "Generic PnP Monitor"** — `szPhysicalMonitorDescription` is always generic. CCD API (`QueryDisplayConfig`) was tried but index matching between CCD paths and DDC/CI monitors is unreliable. The capabilities `model()` tag is the only correct approach.

5. **Code page 936 warning** — `WebViewBridge.cpp` may show C4819 warning in Chinese locale VS. File should be saved as UTF-8 with BOM.

6. **WebView2 runtime required** — users need the Evergreen WebView2 Runtime installed, or the fixed version DLLs bundled.

---

## Build Commands

```powershell
# From VS Developer Command Prompt:
msbuild DDCCI_Tool.sln /t:Build /p:Configuration=Debug /p:Platform=x64

# From WSL (path example):
"/mnt/c/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/amd64/MSBuild.exe" \
  "E:\V3_BAK\work\Code\DDCCI_Tool\DDCCI_Tool.sln" \
  /t:Build /p:Configuration=Debug /p:Platform=x64
```

The post-build step copies `web/` to the output directory (`bin/x64/Debug/web/`).

---

## UI Tabs

| Tab | Purpose |
|-----|---------|
| **Controls** | MCCS-driven controls for every supported VCP code, grouped by functional category (slider / dropdown / read-only / action button by type) + collapsible capabilities string viewer |
| **Log** | DDC/CI read/write log with timestamps, direction arrows (→ send, ← recv), VCP code names, TX/RX hex rows. Clear button. 500-entry cap. |
| **Raw** | Hex body input with real-time TX preview (auto checksum), Send button, response display, quick command presets (11 presets) |

---

## TODO / Pending Tasks

1. **~~Release 前移除 debug.log~~（已完成）** — `MonitorManager.cpp` 和 `WebViewBridge.cpp` 中 `WriteLog`/`BridgeLog` 已用 `#ifdef _DEBUG` 包裹，Release 构建不再写入 `debug.log`。DevTools 同样仅在 Debug 启用。

2. **单文件打包发布** — 当前输出依赖 `web/` 文件夹和 `WebView2Loader.dll`。需将 `web/` 资源嵌入 EXE（通过 RC 资源或嵌入字节数组），`WebView2Loader.dll` 需静态链接或一并打包为单文件

3. **在线检查更新** — 未实现版本更新检测机制。需要：版本号定义、更新服务器 API、下载替换流程、UI 提示。WebView2 已内嵌 Chromium，可直接用 JS `fetch()` 请求更新接口

4. **~~完善 MCCS 所有指令功能解析读写~~（已完成）** — Controls 页面已改为基于 capabilities
   字符串 + MCCS 2.2a 标准解析渲染：`web/mccs.js` 定义全部 152 个 VCP 码（按 8 大功能分组，
   含类型 C/NC/T、读写属性、NC 枚举值表）；`app.js` 的 `parseCapsVCP()` 解析 `vcp(...)`（含子值），
   仅渲染显示器上报且 MCCS 已定义的码，按类型生成滑块 / 下拉 / 只读 / 动作按钮。厂商私有码
   （无 MCCS 定义）不在 Controls 中显示。表（T）类与 LUT/Timing 等结构化指令目前仅做只读展示，
   尚未实现写入编排（可在高级页面中扩展，见第 5 项）。

5. **添加高级/客制化指令页面** — 设计一个高级页面用于自定义 DDC/CI 指令序列：支持多步指令编排、条件执行、延时控制、重复发送等，方便客制化调试和工厂测试场景

6. **未完待续补充** — 其他待定功能（如：监视器热插拔检测、快捷键支持、配置文件导入导出、多语言 i18n、命令行模式、日志导出、VCP 预设方案保存/加载等）
