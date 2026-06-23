# DDCCI Tool — Project Overview

## Summary

Windows desktop application for reading/writing DDC/CI monitor settings (brightness, contrast, input source, color gains, etc.) with a WebView2-based UI.  
Current codebase supports two control paths:

- **Windows dxva2 path** via `MonitorManager` for system-attached monitors
- **Raw I2C path** via `SerialPortManager` + embedded `i2c_dev.dll` for direct DDC/CI transactions

- **Platform**: Windows (`x64` / `Win32`)
- **Language**: C++17 + HTML/CSS/JS
- **Build**: Visual Studio 2022 (Community), MSBuild
- **UI Runtime**: Microsoft Edge WebView2 (NuGet package, statically linked)
- **Raw I2C Runtime**: `i2c_dev.dll` (`x64`) / `i2c_dev_ng.dll` (`Win32`) (embedded as RC resource, extracted at runtime)

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
├── SerialPortManager.h         # Raw I2C / i2c_dev backend declaration
├── SerialPortManager.cpp       # Raw I2C / DDC-CI packet transport via i2c_dev
├── tools/
│   ├── i2c_dev.dll             # Embedded raw I2C backend DLL
│   ├── i2c_dev_ng.dll          # Embedded Win32 raw I2C backend DLL
│   └── make_icon.py            # Regenerates app.ico from the logo design
├── main.cpp                    # WinMain entry point, window class, message loop
├── MonitorManager.h            # Monitor info structs, manager class declaration
├── MonitorManager.cpp          # DDC/CI API wrapper (dxva2)
├── WebViewBridge.h             # WebView2 host + JSON bridge declaration
├── WebViewBridge.cpp           # WebView2 init, JS bridge, DDC/CI packet hex + raw I2C bridge
└── web/
    ├── index.html              # Main page: sidebar + tabs (Controls/Advanced/Log/Raw)
    ├── style.css               # Glass morphism themes (5 themes) + dark mode
    ├── logo.svg                # App logo (monitor + brightness motif, accent blue)
    ├── mccs.js                 # MCCS 2.2a VCP definition table (152 codes)
    └── app.js                  # Frontend logic, bridge, logging, advanced VCP table
```

Output: `bin/x64/Release/DDCCI_Tool.exe` / `bin/Win32/Release/DDCCI_Tool.exe`（单文件分发；运行时自动提取 `web/` 资源和平台对应的 `i2c_dev*.dll`）

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
│          │                    │                 │
│          │                    └──────────────┐  │
│          │                                   │  │
│          │        ┌────────────────────┐     │  │
│          └───────►│ SerialPortManager  │     │  │
│                   │ (i2c_dev.dll)      │     │  │
│                   │ raw DDC/CI over    │     │  │
│                   │ I2C scan/open/read │     │  │
│                   └────────────────────┘     │  │
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
{"method": "enumerateSerialPorts"}
{"method": "openSerialPort", "portName": "i2cdev:<device-name>"}
{"method": "closeSerialPort"}
{"method": "sendSerialRaw", "portName": "i2cdev:<device-name>", "bodyHex": "01 10"}
```

**Response (C++ → JS via ExecuteScript):**
```json
{"type": "monitorList", "monitors": [{"index": 0, "name": "DELL U2723QE"}]}
{"type": "vcpFeature", "monitor": 0, "vcpCode": 16, "current": 50, "max": 100, "sendHex": "...", "recvHex": "..."}
{"type": "capabilities", "monitor": 0, "capabilities": "...", "supportedVCP": [...], "segments": [...], "sendHex": "...", "recvHex": "..."}
{"type": "rawResponse", "monitor": 0, "txHex": "...", "rxHex": "...", "parsed": "..."}
{"type": "serialPortList", "serialPorts": [{"index": 100, "name": "...", "portName": "i2cdev:..."}]}
{"type": "serialPortOpened", "success": true, "portName": "i2cdev:..."}
{"type": "serialRawResponse", "txHex": "...", "rxHex": "...", "parsed": "..."}
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
| `ExtractWebResources(HINSTANCE)` | Extracts embedded web resources to `%LOCALAPPDATA%\DDCCI_Tool\web\` and extracts/loads platform-matched `i2c_dev.dll` / `i2c_dev_ng.dll` to `%LOCALAPPDATA%\DDCCI_Tool\` (or uses files next to exe in dev mode) |
| `HandleRequest(json)` | Dispatches by `method` field: `enumerateMonitors`, `getCapabilities`, `getVCP`, `setVCP`, `sendRaw`, `enumerateSerialPorts`, `openSerialPort`, `closeSerialPort`, `sendSerialRaw` |
| `BuildMonitorList()` | JSON response with all monitor names |
| `BuildGetCapabilitiesResponse(i)` | JSON with caps string, supported VCP codes, segmented send/recv hex |
| `BuildGetVCPResponse(i, vcp)` | JSON with current/max values + computed send/recv hex |
| `BuildSetVCPResponse(i, vcp, val)` | JSON after set + computed send/recv hex |
| `BuildRawCommandResponse(i, bodyHex)` | Parses body hex, dispatches to GetVCP/SetVCP/Capabilities, returns raw hex response |
| `BuildSerialPortList()` | Enumerates `i2c_dev` scan results for raw I2C devices |
| `HandleSendSerialRaw(json)` | Sends raw DDC/CI body over `SerialPortManager` and parses RX packet |

### SerialPortManager (SerialPortManager.h/.cpp)

Raw DDC/CI transport over embedded `i2c_dev` family DLLs.

| Method | Purpose |
|--------|---------|
| `LoadI2CDev(path)` | Dynamically loads platform-matched `i2c_dev.dll` / `i2c_dev_ng.dll` from dev/output/extracted path |
| `EnumeratePorts()` | Calls `i2c_driver_scan` + `i2c_driver_get_scan_result` and exposes devices as `i2cdev:...` |
| `OpenPort(portName)` | Opens selected raw-I2C backend device; for unsupported GPU backends, speed/bulk configuration failures are treated as non-fatal capability gaps |
| `DDCSendRaw(txBody, rxData, error)` | Chooses backend-specific raw DDC flow (`write_read_restart`, `read_ddcci_auto`, or `write` + `read`) and extracts the first valid DDC frame |

Current implementation details:

- Only the `i2c_dev` family is used (`x64` → `i2c_dev.dll`, `Win32` → `i2c_dev_ng.dll`); legacy `ftd2xx.dll` / D2XX / MPSSE code has been removed.
- UI/log 层显示的请求包格式是 `6E 51 [0x80|len] [body...] [chk]`；实际写入 `i2c_dev` 的缓冲区不包含首字节 `0x6E`，而是将 `51 [0x80|len] [body...] [chk]` 发送到 7-bit I2C 地址 `0x37`。
- `SerialPortManager` now distinguishes adapter families by scan name and selects a transport strategy accordingly: GPU-class backends (IGCL / ADL / NVAPI) prefer `i2c_driver_write_read_restart`, IGCL-class devices may additionally fall back to `i2c_driver_read_ddcci_auto`, and FTDI-class adapters continue using `write -> Sleep(70ms) -> read`.
- For scan names that contain formatting noise (for example embedded newlines/spaces in NVAPI results), enumeration keeps a normalized display name for UI use while open still prefers the original raw scan name.
- NVAPI non-standard RAW commands now use a write-first path and then actively attempt to read the reply back; only when the backend truly yields no readable response does the UI fall back to `Write sent successfully (no reply)`.
- On GPU-class backends, `i2c_driver_set_speed` / `i2c_driver_set_enable_bulk` may report `Not Support`; these are treated as backend capability limitations rather than open failures.
- Some `i2c_dev` backends return a repeated packet pattern across the whole read buffer; `SerialPortManager` now trims this to the first valid DDC/CI frame.
- Some GPU backends may return shortened reply buffers without the leading `0x6E`; `SerialPortManager` normalizes these into canonical DDC reply frames before upper-layer parsing.
- Raw reply checksum validation accepts both the traditional `0x6F` seed and the observed `0x50` seed seen on the current `i2c_dev` FTDI-class path.

**DDCI packet hex computations** (anonymous namespace):
- `BuildVCPGetSendHex(vcpCode)` — `6E 51 82 01 [VCP] [CHK]`
- `BuildVCPGetRecvHex(vcpCode, cur, max)` — `6E 88 02 00 [VCP] 00 [MAX_HI] [MAX_LO] [CUR_HI] [CUR_LO] [CHK]`
- `BuildVCPSetSendHex(vcpCode, value)` — `6E 51 84 03 [VCP] [VAL_HI] [VAL_LO] [CHK]`
- `BuildVCPSetRecvHex()` — `6E 81 00 [CHK]`
- `BuildCapsSendHex(offset=0)` — `6E 51 83 F3 [OFF_HI] [OFF_LO] [CHK]`
- `BuildCapsSegRecvHex(offset, nextOffset, chunk)` — per-segment: `6E [0x80|payloadLen] 00 [NEXT_HI] [NEXT_LO] [data...] [CHK]`
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
Displayed request packet (used in UI/log output):
```
[6E] [51] [LEN] [DATA...] [CHK]
```
- `6E` = DDC/CI display write address, `51` = host source address
- `LEN` = `0x80 | (data_byte_count)` — counts the bytes after LEN (including the opcode byte)
- `CHK` = XOR of all preceding bytes (including `6E 51`)

Actual `i2c_dev` write buffer omits the first byte because the bus address is passed separately:
```
I2C addr = 0x37, buffer = [51] [LEN] [DATA...] [CHK]
```

Reply packet format:
```
[6E] [LEN] [DATA...] [CHK]
```
- The reply `LEN` byte also keeps the high bit set (for example `0x88` for a VCP read reply with 8 payload bytes)
- Reply checksum is validated against both seeds currently accepted by the app: `0x6F` and `0x50`

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

## Raw I2C Transport Status

The project now has an implemented raw DDC/CI path instead of only documenting it as a future direction.

- `dxva2` remains the default high-level path for system monitor control (`MonitorManager`)
- `i2c_dev.dll` / `i2c_dev_ng.dll` are the current low-level raw-I2C transports (`SerialPortManager`)
- The raw path has been verified on Intel IGCL, AMD ADL, and NVIDIA NVAPI backends
- NVIDIA / NVAPI has also been verified on the `x64` `i2c_dev.dll` path
- The old hand-written FTDI D2XX / MPSSE experiment is no longer part of the project

Current raw transport characteristics:

- Runtime device discovery comes from `i2c_driver_scan`
- Supported scan results may include IGCL, ADL, NVAPI, and FTDI-class devices in the same list
- Open flow is `scan -> open -> optional speed/bulk config`, where unsupported capability-setting calls are ignored for GPU-class backends
- Preferred DDC flow for GPU-class backends is `write_read_restart`; Intel IGCL may additionally use `read_ddcci_auto`; NVAPI non-standard commands use `write -> post-read attempt`; FTDI-class adapters use `write request -> Sleep(70ms) -> read reply`
- Returned buffers may contain repeated reply frames; code trims to the first valid frame before parsing

---

## Known Issues & Gotchas

1. **`PostWebMessageAsJson` unreliable** — this API sometimes fails to trigger JS `message` events. Workaround: C++ uses `ExecuteScript` to call `window.__bridgeReceive(json)` directly.

2. **JS race condition** — the `message` event listener must be registered only after `chrome.webview` is available. The `init()` function polls with 50ms retries.

3. **`CapabilitiesRequestAndCapabilitiesReply` buffer size** — must pass the exact value returned by `GetCapabilitiesStringLength` (which includes the null terminator). Passing `buf.size()` (which is `len+1`) may cause failures on some monitors.

4. **Monitor names all "Generic PnP Monitor"** — `szPhysicalMonitorDescription` is always generic. CCD API (`QueryDisplayConfig`) was tried but index matching between CCD paths and DDC/CI monitors is unreliable. The capabilities `model()` tag is the only correct approach.

5. **~~Code page 936 warning~~（已修复）** — 已通过 `/utf-8` 编译选项（`AdditionalOptions`）全局解决，源文件保持 UTF-8 无需 BOM。

6. **~~WebView2 运行时依赖~~（已解决）** — WebView2Loader 已静态链接（`WebView2LoaderPreference=Static`），无 `WebView2Loader.dll` 输出。WebView2 Runtime（Evergreen）仍需用户安装，缺失时程序会弹出下载链接提示。

7. **WebView2 UserDataFolder 重定向** — `CreateCoreWebView2EnvironmentWithOptions` 的第二个参数指定为 `%LOCALAPPDATA%\DDCCI_Tool\WV2Data`，避免在 exe 旁生成 `DDCCI_Tool.exe.WebView2` 缓存目录。

8. **`i2c_dev` 扫描结果顺序不固定** — `i2c_driver_scan` 返回的设备顺序可能在不同机器或不同运行间变化，前端应基于 `portName` 选择目标设备，而不是假设固定索引。

9. **`i2c_dev` 读缓冲行为并不总是精确长度返回** — 某些设备会把同一帧 DDC 回复重复填满整个读缓冲，`SerialPortManager` 已在本项目内裁剪首个有效包后再交给上层。

10. **GPU 后端并不统一支持 speed/bulk 配置接口** — IGCL / ADL / NVAPI 这类通过显卡驱动暴露的 I2C/AUX 后端，`i2c_driver_set_speed` 或 `i2c_driver_set_enable_bulk` 可能返回 `Not Support`。当前实现已将其视为“能力不支持”而不是“设备打开失败”。

11. **Release 构建不再落盘 `debug.log`** — 当前工程已将 `SerialPortManager`、`WebViewBridge`、`UpdateChecker` 等日志落盘逻辑限制在 `_DEBUG` 下；Release 构建默认不生成 `debug.log`。

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

Release 构建输出单个 `DDCCI_Tool.exe`，支持 `x64` / `Win32` 两套产物，无额外伴随文件（但系统仍需已安装 WebView2 Runtime）：

| 技术 | 消除的依赖 |
|------|-----------|
| `RuntimeLibrary=MultiThreaded` (/MT) | VC++ Redistributable (VCRUNTIME140.dll) |
| `WebView2LoaderPreference=Static` | WebView2Loader.dll |
| Win32 RC 自定义资源 (WEBRES) | web/ 文件夹、平台对应的 `i2c_dev.dll` / `i2c_dev_ng.dll` |
| `userDataFolder` 重定向 | DDCCI_Tool.exe.WebView2 缓存目录 |

**资源提取策略**：
- 若 exe 旁存在 `web/index.html` → 直接使用（开发模式，Debug 自动复制）
- 否则从 exe 内部资源提取到 `%LOCALAPPDATA%\DDCCI_Tool\web\`（带文件大小缓存检测）
- 平台对应的 `i2c_dev.dll` / `i2c_dev_ng.dll` 也由 exe 内部资源提取到 `%LOCALAPPDATA%\DDCCI_Tool\`
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

1. **~~Release 前移除 debug.log~~（已完成）** — `MonitorManager.cpp`、`SerialPortManager.cpp`、`WebViewBridge.cpp`、`UpdateChecker.cpp` 中的落盘日志已用 `#ifdef _DEBUG` 包裹，Release 构建不再写入 `debug.log`。DevTools 同样仅在 Debug 启用。

2. **~~单文件打包发布~~（已完成）** — Release 构建输出单个 `DDCCI_Tool.exe`，无额外伴随文件：
   - CRT 静态链接（/MT），WebView2Loader 静态链接，`web/` 与平台对应的 `i2c_dev.dll` / `i2c_dev_ng.dll` 均嵌入为 RC 资源（WEBRES）
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
