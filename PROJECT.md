# DDCCI Tool — Project Overview

## Summary

Windows desktop application for reading/writing DDC/CI monitor settings (brightness, contrast, input source, color gains, etc.) with a WebView2-based dark-themed UI.

- **Platform**: Windows (x64)
- **Language**: C++17 + HTML/CSS/JS
- **Build**: Visual Studio 2022 (Community), MSBuild
- **UI Runtime**: Microsoft Edge WebView2 (NuGet package, statically linked)
- **Total source**: ~4100 lines across 9 files

---

## Project Structure

```
DDCCI_Tool/
├── DDCCI_Tool.sln              # VS2022 solution
├── DDCCI_Tool.vcxproj          # MSBuild project
├── packages.config             # NuGet: Microsoft.Web.WebView2 1.0.2903.40
├── resource.h                  # RC resource IDs
├── DDCCI_Tool.rc               # Executable metadata (icon, version) + embedded web resources (WEBRES)
├── app.ico                     # App icon (6 sizes 16–256, PNG-in-ICO)
├── tools/
│   └── make_icon.py            # Regenerates app.ico from the logo design
├── main.cpp                    # WinMain entry point, window class, message loop     (98 lines)
├── MonitorManager.h            # Monitor info structs, manager class declaration     (46 lines)
├── MonitorManager.cpp          # DDC/CI API wrapper (dxva2)                          (272 lines)
├── WebViewBridge.h             # WebView2 host + JSON bridge declaration             (46 lines)
├── WebViewBridge.cpp           # WebView2 init, JS bridge, DDC/CI packet hex         (646 lines)
└── web/
    ├── index.html              # Main page: sidebar + tabs (Controls/Advanced/Log/Raw)(138 lines)
    ├── style.css               # Glass morphism themes (5 themes) + dark mode         (1302 lines)
    ├── logo.svg                # App logo (monitor + brightness motif, accent blue)
    ├── mccs.js                 # MCCS 2.2a VCP definition table (152 codes)          (438 lines)
    └── app.js                  # Frontend logic, bridge, logging, advanced VCP table  (1092 lines)
```

Output: `bin/x64/Release/DDCCI_Tool.exe`（单文件，无外部依赖）

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
| `ExtractWebResources(HINSTANCE)` | Extracts embedded web resources to `%LOCALAPPDATA%\DDCCI_Tool\web\` (or uses `web/` next to exe in dev mode) |
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
4. `WM_CREATE` → `bridge.ExtractWebResources()` then `bridge.Initialize(hwnd)`
5. `WM_SIZE` → `bridge.Resize()`
6. `WM_DESTROY` → `bridge.Close()`, `PostQuitMessage`

### web/ (Frontend)

#### index.html
- Sidebar: monitor list + refresh button + status bar
- Main panel: Controls tab (VCP sliders + capabilities viewer), **Advanced tab (raw VCP table)**, Log tab, Raw command tab

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
- **Tab switching**: Controls | **Advanced** | Log | Raw
- **Advanced tab**: Lists all VCP codes from capabilities (without MCCS filtering), showing raw MH-ML / SH-SL / MH-ML-SH-SL values in hex+decimal+binary format. SET column with Set10 (decimal) / Set16 (hex) buttons for writing values. GET column with per-row Get button for refreshing individual VCP reads. Data auto-updates when `vcpFeature` responses arrive.
- **Bridge**: `window.__bridgeReceive(data)` called by C++ via ExecuteScript, dispatches to `dispatchResponse()`

#### mccs.js
`window.MCCS = { groups: [...], vcp: {...} }`. The `groups` array orders the 8 MCCS
functional categories (Preset / Image / Color / Display / Geometry / Misc / Audio / DPVL).
Each `vcp[code]` has `{ name, group, access:'rw'|'ro'|'wo', type:'C'|'NC'|'T', values?, action? }`.
`values` maps value → label for enumerable NC codes (e.g. Input Source 0x60, Color Preset
0x14, Power Mode 0xD6, OSD Language 0xCC). `action:true` marks WO trigger codes
(restore-defaults family, degauss). Generated from `SPEC/MCCS 2.2a.pdf` Section 8 tables.

#### style.css
Glass morphism theme system with 5 switchable themes (Glass/Apple, Graphite Gold, Nord Frost, Cyber Neon, Light Mode) using CSS variables (`--bg-base`, `--glass-bg`, `--accent`, `--text-primary/secondary/muted` etc.). Legacy aliases for backward compatibility. Dark theme default: `--bg-primary: #1e1e2e`, `--accent: #5b8df0`, `--sidebar-width: 220px`. Scrollbar styling, slider thumb styling, monospace log entries. Advanced table styling with sticky headers and monospace data cells.

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

## 后续扩展 / Future Extensions

### 精确控制 DDC/CI 读时序（裸 I2C 传输层）

**背景**：当前工具通过 Windows 高层 API `dxva2.lib`（`GetVCPFeatureAndVCPFeatureReply`、
`CapabilitiesRequestAndCapabilitiesReply`）读写 DDC/CI。该调用把「发读请求 → 等待 → 读回复」
**封装在一次原子系统调用内部**，应用层无任何插入点：

```
GetVCPFeatureAndVCPFeatureReply()   ← 一次调用
  ├─ write（发读请求）        ┐
  ├─ 内部等待 ~55ms           │ 全在显卡驱动/OS 内部，应用层不可见、不可改
  └─ read（读回复）           ┘
```

- 我们能加的 `Sleep` 只能落在**调用之前**或**之后**，是**叠加**在驱动内部 ~55ms 之上的空闲时间，
  逻辑分析仪上 write→read 的间隔**不变**。
- 因此「指令等待时间」这类设置在 dxva2 路径下**无法真正生效**（已于本轮回退删除）。
- 若「读时序可控」成为硬需求，必须**绕开 dxva2，改用裸 I2C 分步事务**：
  `i2c_write(读请求) → Sleep(delayMs) → i2c_read(回复)`，三步独立，等待时间即可 1:1 映射到线上。

**方案 2 — 改用裸 I2C 传输层**。按显卡 / 接入方式选择后端：

| 后端 | 适用场景 | 关键 API |
|------|----------|----------|
| **IGCL**（Intel Graphics Control Library） | Intel 核显 / Arc 独显，直连真实显示器 | I2C AUX：write → 自定义等待 → read |
| **AMD ADL** / **NVIDIA NvAPI** | 对应 A 卡 / N 卡 | 各家 I2C 透传接口 |
| **CH341 / FTDI USB-I2C 转接板** | 脱机 / 产线 / 工厂测试 | `i2c_write` / `i2c_read`，`CH341SetDelaymS` |

实现要点：抽象出 `IDdcTransport` 接口（`Write` / `Read` / `SetReplyDelayMs`），
`MonitorManager` 的 Get/Set/Capabilities 改为「写请求 → 延时 → 读回复」分步调用；
UI 恢复「读回复等待时间(ms)」设置项，其值传到传输层的 `SetReplyDelayMs`。

**方案 3 — 参考 CVTE JyCLI 实现**。`D:\MonitorDockFiles\PackageCache\JyCLI\1.5.1\JyCLI.exe`
（CVTE 绝影架构命令行测试工具）已用同样思路，可作为实现参考：

- 传输层依赖 `MyCL.I2c.NoDrv`（无驱动直连 I2C 库）。
- 走 **IGCL** 直连真实显示器（`I2CController::aux_write_read_restart`、`aux_read_auto_ddcci`），
  同时支持 **CH341 / FTDI** USB-I2C 转接板（`CH341_IIC::i2c_write/i2c_read`、`CH341SetDelaymS`）。
- 其读函数内联了 `<Read>g__DelayMs` —— 即 write 与 read 之间的可配置等待，正是逻辑分析仪量到的
  ~55ms 那段。
- 待办：反编译其托管程序集（单文件压缩打包，`strings` 取不到默认延时值与完整命令表），
  扒出默认 DelayMs 值、IGCL/CH341 的 I2C 调用序列与 DDC/CI 时序参数，作为方案 2 的落地依据。

---

## Known Issues & Gotchas

1. **`PostWebMessageAsJson` unreliable** — this API sometimes fails to trigger JS `message` events. Workaround: C++ uses `ExecuteScript` to call `window.__bridgeReceive(json)` directly.

2. **JS race condition** — the `message` event listener must be registered only after `chrome.webview` is available. The `init()` function polls with 50ms retries.

3. **`CapabilitiesRequestAndCapabilitiesReply` buffer size** — must pass the exact value returned by `GetCapabilitiesStringLength` (which includes the null terminator). Passing `buf.size()` (which is `len+1`) may cause failures on some monitors.

4. **Monitor names all "Generic PnP Monitor"** — `szPhysicalMonitorDescription` is always generic. CCD API (`QueryDisplayConfig`) was tried but index matching between CCD paths and DDC/CI monitors is unreliable. The capabilities `model()` tag is the only correct approach.

5. **~~Code page 936 warning~~（已修复）** — 已通过 `/utf-8` 编译选项（`AdditionalOptions`）全局解决，源文件保持 UTF-8 无需 BOM。

6. **~~WebView2 运行时依赖~~（已解决）** — WebView2Loader 已静态链接（`WebView2LoaderPreference=Static`），无 `WebView2Loader.dll` 输出。WebView2 Runtime（Evergreen）仍需用户安装，缺失时程序会弹出下载链接提示。

7. **WebView2 UserDataFolder 重定向** — `CreateCoreWebView2EnvironmentWithOptions` 的第二个参数指定为 `%LOCALAPPDATA%\DDCCI_Tool\WV2Data`，避免在 exe 旁生成 `DDCCI_Tool.exe.WebView2` 缓存目录。

---

## Build Commands

```powershell
# From VS Developer Command Prompt:
msbuild DDCCI_Tool.sln /t:Build /p:Configuration=Debug /p:Platform=x64

# Release build (single-file output):
msbuild DDCCI_Tool.sln /t:Rebuild /p:Configuration=Release /p:Platform=x64

# From WSL (path example):
"/mnt/c/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/amd64/MSBuild.exe" \
  "E:\V3_BAK\work\Code\DDCCI_Tool\DDCCI_Tool.sln" \
  /t:Rebuild /p:Configuration=Release /p:Platform=x64
```

### Single-File Packaging

Release 构建输出单个 `DDCCI_Tool.exe`，无外部依赖文件：

| 技术 | 消除的依赖 |
|------|-----------|
| `RuntimeLibrary=MultiThreaded` (/MT) | VC++ Redistributable (VCRUNTIME140.dll) |
| `WebView2LoaderPreference=Static` | WebView2Loader.dll |
| Win32 RC 自定义资源 (WEBRES) | web/ 文件夹 |
| `userDataFolder` 重定向 | DDCCI_Tool.exe.WebView2 缓存目录 |

**资源提取策略**：
- 若 exe 旁存在 `web/index.html` → 直接使用（开发模式，Debug 自动复制）
- 否则从 exe 内部资源提取到 `%LOCALAPPDATA%\DDCCI_Tool\web\`（带文件大小缓存检测）
- WebView2 数据目录 → `%LOCALAPPDATA%\DDCCI_Tool\WV2Data\`

---

## UI Tabs

| Tab | Purpose |
|-----|---------|
| **Controls** | MCCS-driven controls for every supported VCP code, grouped by functional category (slider / dropdown / read-only / action button by type) + collapsible capabilities string viewer |
| **Advanced** | Raw VCP code table listing all codes from capabilities without MCCS filtering. Shows MH-ML / SH-SL / MH-ML-SH-SL in hex+decimal+binary. SET column (Set10 decimal / Set16 hex) + GET column (per-row read refresh) |
| **Log** | DDC/CI read/write log with timestamps, direction arrows (→ send, ← recv), VCP code names, TX/RX hex rows. Clear button. 500-entry cap. |
| **Raw** | Hex body input with real-time TX preview (auto checksum), Send button, response display, quick command presets (11 presets) |

---

## TODO / Pending Tasks

1. **~~Release 前移除 debug.log~~（已完成）** — `MonitorManager.cpp` 和 `WebViewBridge.cpp` 中 `WriteLog`/`BridgeLog` 已用 `#ifdef _DEBUG` 包裹，Release 构建不再写入 `debug.log`。DevTools 同样仅在 Debug 启用。

2. **~~单文件打包发布~~（已完成）** — Release 构建输出单个 `DDCCI_Tool.exe`：
   - CRT 静态链接（/MT），WebView2Loader 静态链接，web 文件嵌入为 RC 资源（WEBRES）
   - 运行时自动提取到 `%LOCALAPPDATA%\DDCCI_Tool\`，WebView2 数据目录重定向避免 exe 旁生成缓存文件夹

3. **在线检查更新** — 未实现版本更新检测机制。需要：版本号定义、更新服务器 API、下载替换流程、UI 提示。WebView2 已内嵌 Chromium，可直接用 JS `fetch()` 请求更新接口

4. **~~完善 MCCS 所有指令功能解析读写~~（已完成）** — Controls 页面已改为基于 capabilities
   字符串 + MCCS 2.2a 标准解析渲染：`web/mccs.js` 定义全部 152 个 VCP 码（按 8 大功能分组，
   含类型 C/NC/T、读写属性、NC 枚举值表）；`app.js` 的 `parseCapsVCP()` 解析 `vcp(...)`（含子值），
   仅渲染显示器上报且 MCCS 已定义的码，按类型生成滑块 / 下拉 / 只读 / 动作按钮。厂商私有码
   （无 MCCS 定义）不在 Controls 中显示。表（T）类与 LUT/Timing 等结构化指令目前仅做只读展示，
   尚未实现写入编排（可在高级页面中扩展，见第 5 项）。

5. **~~添加高级/客制化指令页面~~（已部分完成）** — 已实现 Advanced 标签页，列出显示器 capabilities 中所有 VCP Code 的原始数据（MH-ML/SH-SL/MH-ML-SH-SL hex+decimal+binary），支持按十进制或十六进制发送 setVCP，以及单条 VCP 读取刷新。待扩展：多步指令编排、条件执行、延时控制、重复发送等客制化调试和工厂测试功能

6. **未完待续补充** — 其他待定功能（如：监视器热插拔检测、快捷键支持、配置文件导入导出、多语言 i18n、命令行模式、日志导出、VCP 预设方案保存/加载等）
