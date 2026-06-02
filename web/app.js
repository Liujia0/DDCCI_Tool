(function () {
    'use strict';

    // ---- MCCS table accessors ----
    // The full VCP definition table lives in mccs.js (window.MCCS), loaded first.

    var MCCS = window.MCCS || { groups: [], vcp: {} };

    function hex2(code) {
        return '0x' + ('0' + code.toString(16).toUpperCase()).slice(-2);
    }

    function vcpDef(code) {
        return MCCS.vcp[code] || null;
    }

    function vcpCodeName(code) {
        var def = vcpDef(code);
        return def ? def.name : hex2(code);
    }

    // ---- State ----

    var monitors = [];
    var activeMonitorIndex = -1;
    var currentCapsMap = {};   // {vcpCode: [subValues]} parsed from capabilities

    // ---- DOM refs ----

    var monitorListEl = document.getElementById('monitor-list');
    var btnRefreshEl = document.getElementById('btn-refresh');
    var statusTextEl = document.getElementById('status-text');
    var welcomeEl = document.getElementById('welcome');
    var controlsEl = document.getElementById('controls');
    var monitorNameEl = document.getElementById('monitor-name');
    var vcpControlsEl = document.getElementById('vcp-controls');
    var capsToggleEl = document.getElementById('caps-toggle');
    var capsContentEl = document.getElementById('caps-content');
    var tabControls = document.getElementById('tab-controls');
    var tabLog = document.getElementById('tab-log');
    var tabRaw = document.getElementById('tab-raw');
    var logEntriesEl = document.getElementById('log-entries');
    var btnClearLogEl = document.getElementById('btn-clear-log');
    var scanOverlayEl = document.getElementById('scan-overlay');

    // ---- Log ----
    //
    // Each read/write operation is ONE record. The send half (logical content +
    // send time) is recorded the instant the request leaves the bridge; the
    // receive half (result detail, RX bytes, receive time) — plus the actual TX
    // bytes, which the host only computes on response — is filled in when the
    // matching response arrives. Sends are paired to responses FIFO within a key
    // (method + vcpCode) so out-of-order responses still land on the right card.

    var logRecords = [];
    var MAX_LOG = 500;
    var pendingByKey = {};   // pairing key -> [record, ...] awaiting a response

    function nowStamp() {
        var now = new Date();
        return ('0' + now.getHours()).slice(-2) + ':'
             + ('0' + now.getMinutes()).slice(-2) + ':'
             + ('0' + now.getSeconds()).slice(-2) + '.'
             + ('00' + now.getMilliseconds()).slice(-3);
    }

    function pushRecord(rec) {
        logRecords.push(rec);
        if (logRecords.length > MAX_LOG) {
            var dropped = logRecords.shift();
            // Drop any pending reference to the evicted record so we don't pair
            // a future response onto a card that no longer exists.
            for (var key in pendingByKey) {
                if (!pendingByKey.hasOwnProperty(key)) continue;
                var idx = pendingByKey[key].indexOf(dropped);
                if (idx >= 0) pendingByKey[key].splice(idx, 1);
            }
        }
        renderIfVisible();
    }

    function renderIfVisible() {
        if (!tabLog.classList.contains('hidden')) renderLogEntries();
    }

    function escapeHtml(s) {
        return String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
    }

    // Map a response type back to the request method that produced it, so
    // responses can be matched to their pending send.
    function typeToMethod(type) {
        switch (type) {
        case 'monitorList':   return 'enumerateMonitors';
        case 'vcpFeature':    return 'getVCP';
        case 'vcpSet':        return 'setVCP';
        case 'capabilities':  return 'getCapabilities';
        case 'rawResponse':   return 'sendRaw';
        default:              return type;
        }
    }

    function pairKey(method, vcpCode) {
        if (method === 'getVCP' || method === 'setVCP') {
            return method + ':' + vcpCode;
        }
        return method;
    }

    function renderLogEntries() {
        logEntriesEl.innerHTML = '';
        for (var i = 0; i < logRecords.length; i++) {
            logEntriesEl.appendChild(renderLogCard(logRecords[i]));
        }
        logEntriesEl.scrollTop = logEntriesEl.scrollHeight;
    }

    function renderLogCard(r) {
        var card = document.createElement('div');
        card.className = 'log-card';

        var html = '<div class="log-card-head">'
            + '<span class="log-op">' + escapeHtml(r.op) + '</span>'
            + (r.label ? '<span class="log-detail">' + escapeHtml(r.label) + '</span>' : '')
            + '</div>';

        // TX line: send time + sent bytes.
        html += '<div class="log-line tx">'
            + '<span class="log-dir send">TX</span>'
            + '<span class="log-time">' + r.send.time + '</span>'
            + '<span class="log-hex">' + escapeHtml(r.send.hex || '—') + '</span>'
            + '</div>';

        // RX line: receive time + result detail + received bytes (or a
        // placeholder while the response is still outstanding).
        if (r.recv) {
            html += '<div class="log-line rx">'
                + '<span class="log-dir recv">RX</span>'
                + '<span class="log-time">' + r.recv.time + '</span>'
                + (r.recv.detail ? '<span class="log-detail">' + escapeHtml(r.recv.detail) + '</span>' : '')
                + (r.recv.hex ? '<span class="log-hex">' + escapeHtml(r.recv.hex) + '</span>' : '')
                + '</div>';
        } else {
            html += '<div class="log-line rx pending">'
                + '<span class="log-dir recv">RX</span>'
                + '<span class="log-detail">等待中…</span>'
                + '</div>';
        }

        card.innerHTML = html;
        return card;
    }

    function vcpLabel(code) {
        return vcpCodeName(code) + ' (' + hex2(code) + ')';
    }

    // Record the send half of an operation and queue it for pairing.
    function logSend(method, params) {
        var label = '';
        if (method === 'getVCP' || method === 'setVCP') {
            label = vcpLabel(params.vcpCode);
            if (method === 'setVCP') label += ' = ' + params.value;
        } else if (method === 'enumerateMonitors') {
            label = 'Scan monitors';
        } else if (method === 'getCapabilities') {
            label = 'Monitor ' + params.monitor;
        }
        var rec = {
            op: method,
            label: label,
            send: { time: nowStamp(), hex: '' },  // TX bytes filled on response
            recv: null
        };
        pushRecord(rec);
        var key = pairKey(method, params && params.vcpCode);
        (pendingByKey[key] = pendingByKey[key] || []).push(rec);
    }

    // Fill in the receive half (and the now-known TX bytes) on the matching
    // pending record.
    function logRecv(data) {
        var detail = '';
        if (data.type === 'monitorList') {
            detail = (data.monitors ? data.monitors.length : 0) + ' monitor(s)';
        } else if (data.type === 'vcpFeature') {
            if (data.valid === false) {
                detail = vcpLabel(data.vcpCode) + ' = <read failed>';
            } else {
                detail = vcpLabel(data.vcpCode) + ' = ' + data.current + '/' + data.max;
            }
        } else if (data.type === 'vcpSet') {
            detail = vcpLabel(data.vcpCode) + ' = ' + data.value;
        } else if (data.type === 'capabilities') {
            var preview = (data.capabilities || '').substring(0, 60);
            detail = preview + (data.capabilities && data.capabilities.length > 60 ? '...' : '');
        } else if (data.error) {
            detail = 'ERROR: ' + data.error;
        }

        var method = typeToMethod(data.type);
        var key = pairKey(method, data.vcpCode);
        var queue = pendingByKey[key];
        var rec = (queue && queue.length) ? queue.shift() : null;

        if (!rec) {
            // No matching send (e.g. a host-initiated message). Show it as a
            // recv-only card rather than dropping it.
            rec = { op: data.type || 'response', label: '',
                    send: { time: nowStamp(), hex: '' }, recv: null };
            pushRecord(rec);
        }

        if (data.sendHex) rec.send.hex = data.sendHex;
        rec.recv = { time: nowStamp(), hex: data.recvHex || '', detail: detail };
        renderIfVisible();
    }

    // ---- Bridge helpers ----

    function sendToHost(method, params) {
        var msg = { method: method };
        if (params) {
            for (var k in params) {
                if (params.hasOwnProperty(k)) msg[k] = params[k];
            }
        }
        logSend(method, params || {});
        if (window.chrome && window.chrome.webview) {
            window.chrome.webview.postMessage(JSON.stringify(msg));
        }
    }

    function onHostMessage(event) {
        var data;
        try {
            data = JSON.parse(event.data);
        } catch (e) {
            return;
        }
        dispatchResponse(data);
    }

    function dispatchResponse(data) {
        logRecv(data);

        switch (data.type) {
        case 'monitorList':
            monitors = data.monitors || [];
            renderMonitorList();
            // Keep the scan animation up briefly so it reads as a real scan
            // rather than a flicker on machines that enumerate instantly.
            setTimeout(hideScanOverlay, 450);
            if (activeMonitorIndex >= monitors.length) {
                selectMonitor(-1);
            } else if (activeMonitorIndex >= 0) {
                selectMonitor(activeMonitorIndex);
            }
            break;

        case 'vcpFeature':
            updateVCPUI(data.vcpCode, data.current, data.max, data.valid !== false);
            break;

        case 'vcpSet':
            setStatus('Set OK');
            break;

        case 'capabilities':
            capsContentEl.textContent = data.capabilities || '(empty)';

            // Parse the capabilities string against the MCCS standard. The
            // controls shown are driven entirely by vcp(...) — codes the
            // monitor does not report are not rendered, and codes with no
            // MCCS 2.2a definition (manufacturer-specific) are hidden.
            var capsMap = parseCapsVCP(data.capabilities || '');
            if (isEmptyMap(capsMap) && data.supportedVCP) {
                // Fallback: caps string unreadable but host parsed a code list.
                for (var s = 0; s < data.supportedVCP.length; s++) {
                    capsMap[data.supportedVCP[s]] = [];
                }
            }
            currentCapsMap = capsMap;
            buildVCPControls(capsMap);

            // Log each segment's send & receive as its own paired card, BEFORE
            // dispatching the per-VCP reads below. The segments are the wire
            // detail of this single capabilities read, so they must sit right
            // under the getCapabilities summary card — not after the getVCP
            // cards that queryAllVCPFeatures() is about to push. Both halves of
            // a segment exchange are already known, so each card is complete on
            // creation.
            if (data.segments) {
                for (var seg = 0; seg < data.segments.length; seg++) {
                    var sdata = data.segments[seg];
                    var label = 'caps[' + seg + ']';
                    if (data.segments.length > 1) label += ' ' + (seg + 1) + '/' + data.segments.length;
                    pushRecord({
                        op: label,
                        label: '',
                        send: { time: nowStamp(), hex: sdata.sendHex || '' },
                        recv: { time: nowStamp(), hex: sdata.recvHex || '',
                                detail: sdata.recvHex ? '' : '<read failed>' }
                    });
                }
            }

            queryAllVCPFeatures(capsMap);
            break;

        case 'rawResponse':
            displayRawResponse(data);
            break;

        default:
            if (data.error) {
                setStatus('Error: ' + data.error);
            }
        }
    }

    // Called directly by C++ via ExecuteScript (bypasses PostWebMessageAsJson)
    window.__bridgeReceive = function(data) {
        dispatchResponse(data);
    };

    // ---- Capabilities parsing ----

    function isEmptyMap(obj) {
        for (var k in obj) { if (obj.hasOwnProperty(k)) return false; }
        return true;
    }

    // Parse the vcp(...) section of a capabilities string, including the
    // per-code sub-value lists. e.g. "vcp(10 12 14(05 0B) 60(0F 11 12))"
    //   -> { 16:[], 18:[], 20:[5,11], 96:[15,17,18] }   (keys are decimal)
    function parseCapsVCP(caps) {
        var map = {};
        if (!caps) return map;
        var pos = caps.indexOf('vcp(');
        if (pos < 0) return map;
        var i = pos + 4;
        var depth = 1;
        var curCode = -1;
        var token = '';

        function flush() {
            if (token.length === 0) return;
            var val = parseInt(token, 16);
            token = '';
            if (isNaN(val)) return;
            if (depth === 1) {
                curCode = val;
                if (!map.hasOwnProperty(val)) map[val] = [];
            } else if (depth === 2 && curCode >= 0) {
                map[curCode].push(val);
            }
        }

        while (i < caps.length && depth > 0) {
            var c = caps.charAt(i);
            if (c === '(') { flush(); depth++; i++; }
            else if (c === ')') { flush(); depth--; i++; }
            else if (/[0-9a-fA-F]/.test(c)) { token += c; i++; }
            else { flush(); i++; }   // whitespace / separators
        }
        return map;
    }

    // ---- VCP controls (MCCS-driven) ----

    function buildVCPControls(capsMap) {
        vcpControlsEl.innerHTML = '';

        var rendered = 0;
        for (var g = 0; g < MCCS.groups.length; g++) {
            var group = MCCS.groups[g];

            // Collect supported, MCCS-defined codes belonging to this group.
            var codes = [];
            for (var codeStr in capsMap) {
                if (!capsMap.hasOwnProperty(codeStr)) continue;
                var code = parseInt(codeStr, 10);
                var def = vcpDef(code);
                if (def && def.group === group.id) codes.push(code);
            }
            if (codes.length === 0) continue;
            codes.sort(function (a, b) { return a - b; });

            var section = document.createElement('div');
            section.className = 'vcp-group';

            var title = document.createElement('h3');
            title.className = 'vcp-group-title';
            title.textContent = group.title;
            section.appendChild(title);

            for (var k = 0; k < codes.length; k++) {
                var row = buildVCPRow(codes[k], capsMap[codes[k]]);
                if (row) { section.appendChild(row); rendered++; }
            }
            vcpControlsEl.appendChild(section);
        }

        if (rendered === 0) {
            var empty = document.createElement('div');
            empty.className = 'vcp-empty';
            empty.textContent = 'No MCCS-defined controls reported by this monitor.';
            vcpControlsEl.appendChild(empty);
        }
    }

    function buildVCPRow(code, subvals) {
        var def = vcpDef(code);
        if (!def) return null;

        var row = document.createElement('div');
        row.className = 'vcp-row';

        var label = document.createElement('span');
        label.className = 'vcp-label';
        label.textContent = vcpLabel(code);
        row.appendChild(label);

        if (def.access === 'wo') {
            appendAction(row, code, def);
        } else if (def.type === 'NC' && (def.values || (subvals && subvals.length))) {
            // Enumerated: either MCCS defines named values, or the monitor
            // itself enumerated discrete sub-values in its capabilities.
            if (def.access === 'ro') {
                appendReadonly(row, code);
            } else {
                appendSelect(row, code, def, subvals);
            }
        } else if (def.type === 'C' || def.type === 'NC') {
            // Continuous, or NC without an enumerable value set (e.g. volume):
            // a byte-valued range. Read-only codes show a value, R/W get a slider.
            if (def.access === 'ro') {
                appendReadonly(row, code);
            } else {
                appendSlider(row, code);
            }
        } else {
            // Table (T) codes carry structured data — show the raw value only.
            appendReadonly(row, code);
        }
        return row;
    }

    function appendSlider(row, code) {
        var slider = document.createElement('input');
        slider.type = 'range';
        slider.className = 'vcp-slider';
        slider.min = 0;
        slider.max = 100;
        slider.value = 0;
        slider.disabled = true;   // enabled once a value is read
        slider.dataset.vcpCode = code;

        var valSpan = document.createElement('span');
        valSpan.className = 'vcp-value';
        valSpan.id = 'val-' + code;
        valSpan.textContent = '--';

        slider.addEventListener('input', function () {
            var span = document.getElementById('val-' + code);
            if (span) span.textContent = slider.value;
        });
        slider.addEventListener('change', function () {
            sendToHost('setVCP', { monitor: activeMonitorIndex, vcpCode: code, value: parseInt(slider.value) });
        });

        row.appendChild(slider);
        row.appendChild(valSpan);
    }

    function appendSelect(row, code, def, subvals) {
        var select = document.createElement('select');
        select.className = 'vcp-select';
        select.disabled = true;
        select.dataset.vcpCode = code;

        var placeholder = document.createElement('option');
        placeholder.value = '';
        placeholder.textContent = 'Detecting...';
        select.appendChild(placeholder);

        var labelMap = def.values || {};

        // Options: the values the monitor enumerated in caps, or — if it
        // listed none — all values MCCS defines for this code. Labels come
        // from the MCCS table, falling back to the raw hex value.
        var values;
        if (subvals && subvals.length) {
            values = subvals;
        } else {
            values = Object.keys(labelMap).map(function (k) { return parseInt(k, 10); });
        }
        for (var i = 0; i < values.length; i++) {
            var v = values[i];
            var opt = document.createElement('option');
            opt.value = String(v);
            opt.textContent = labelMap[v] || hex2(v);
            select.appendChild(opt);
        }

        select.addEventListener('change', function () {
            if (select.value !== '') {
                sendToHost('setVCP', { monitor: activeMonitorIndex, vcpCode: code, value: parseInt(select.value) });
            }
        });

        row.appendChild(select);
    }

    function appendReadonly(row, code) {
        var span = document.createElement('span');
        span.className = 'vcp-readonly';
        span.id = 'val-' + code;
        span.dataset.vcpCode = code;
        span.textContent = '--';
        row.appendChild(span);
    }

    function appendAction(row, code, def) {
        var wrap = document.createElement('span');
        wrap.className = 'vcp-actions';

        if (def.values) {
            // One button per documented action value (e.g. Save / Restore).
            var keys = Object.keys(def.values).map(function (k) { return parseInt(k, 10); });
            for (var i = 0; i < keys.length; i++) {
                (function (v) {
                    var btn = document.createElement('button');
                    btn.className = 'vcp-action-btn';
                    btn.textContent = def.values[v];
                    btn.addEventListener('click', function () {
                        sendToHost('setVCP', { monitor: activeMonitorIndex, vcpCode: code, value: v });
                    });
                    wrap.appendChild(btn);
                })(keys[i]);
            }
        } else {
            // Trigger action — any non-zero value performs it.
            var btn = document.createElement('button');
            btn.className = 'vcp-action-btn';
            btn.textContent = 'Apply';
            btn.addEventListener('click', function () {
                sendToHost('setVCP', { monitor: activeMonitorIndex, vcpCode: code, value: 1 });
            });
            wrap.appendChild(btn);
        }
        row.appendChild(wrap);
    }

    function queryAllVCPFeatures(capsMap) {
        for (var codeStr in capsMap) {
            if (!capsMap.hasOwnProperty(codeStr)) continue;
            var code = parseInt(codeStr, 10);
            var def = vcpDef(code);
            if (!def) continue;
            if (def.access === 'ro' || def.access === 'rw') {
                sendToHost('getVCP', { monitor: activeMonitorIndex, vcpCode: code });
            }
        }
    }

    function updateVCPUI(code, current, max, valid) {
        var def = vcpDef(code);

        // Slider
        var slider = document.querySelector('.vcp-slider[data-vcp-code="' + code + '"]');
        if (slider) {
            var sVal = document.getElementById('val-' + code);
            if (valid === false) {
                slider.disabled = true;
                if (sVal) sVal.textContent = 'err';
            } else {
                slider.disabled = false;
                slider.max = max || 100;
                slider.value = current;
                if (sVal) sVal.textContent = current;
            }
        }

        // Select
        var select = document.querySelector('.vcp-select[data-vcp-code="' + code + '"]');
        if (select) {
            if (valid === false) {
                select.disabled = true;
            } else {
                select.disabled = false;
                select.value = String(current);
                if (select.value !== String(current)) {
                    // Monitor reported a value not in the option list — add it.
                    var opt = document.createElement('option');
                    opt.value = String(current);
                    var nm = (def && def.values && def.values[current]) || ('Reserved (' + hex2(current) + ')');
                    opt.textContent = nm;
                    select.appendChild(opt);
                    select.value = String(current);
                }
            }
        }

        // Read-only display
        var ro = document.querySelector('.vcp-readonly[data-vcp-code="' + code + '"]');
        if (ro) {
            if (valid === false) {
                ro.textContent = '<read failed>';
            } else if (def && def.type === 'C') {
                ro.textContent = max ? (current + ' / ' + max) : String(current);
            } else if (def && def.values && def.values[current]) {
                ro.textContent = def.values[current] + ' (' + hex2(current) + ')';
            } else {
                ro.textContent = current + ' (' + hex2(current) + ')';
            }
        }
    }

    // ---- Raw command tab ----

    var rawHexInput = document.getElementById('raw-hex-input');
    var rawTxHex = document.getElementById('raw-tx-hex');
    var btnRawSend = document.getElementById('btn-raw-send');
    var rawResponse = document.getElementById('raw-response');
    var rawQuickList = document.getElementById('raw-quick-list');

    function computeTxHex() {
        var hex = rawHexInput.value.replace(/\s+/g, ' ').trim();
        var parts = hex.split(' ').filter(Boolean);
        var body = [];
        for (var i = 0; i < parts.length; i++) {
            var b = parseInt(parts[i], 16);
            if (isNaN(b) || b < 0 || b > 255) { rawTxHex.textContent = ''; return null; }
            body.push(b);
        }
        if (body.length === 0) { rawTxHex.textContent = ''; return null; }

        // Build full TX: 6E 51 [LEN=0x80|n] [body...] [CHK]
        var tx = [0x6E, 0x51, 0x80 | (body.length & 0x7F)];
        for (var j = 0; j < body.length; j++) tx.push(body[j]);
        var chk = 0;
        for (var k = 0; k < tx.length; k++) chk ^= tx[k];
        tx.push(chk);

        var hexParts = [];
        for (var m = 0; m < tx.length; m++) {
            hexParts.push(('0' + tx[m].toString(16).toUpperCase()).slice(-2));
        }
        rawTxHex.textContent = hexParts.join(' ');
        return body;
    }

    rawHexInput.addEventListener('input', computeTxHex);

    btnRawSend.addEventListener('click', function () {
        if (activeMonitorIndex < 0) {
            rawResponse.innerHTML = '<span class="raw-resp-error">No monitor selected</span>';
            return;
        }
        var body = computeTxHex();
        if (!body) {
            rawResponse.innerHTML = '<span class="raw-resp-error">Invalid hex input</span>';
            return;
        }
        rawResponse.innerHTML = '<span class="raw-placeholder">Sending...</span>';
        sendToHost('sendRaw', { monitor: activeMonitorIndex, bodyHex: rawHexInput.value.trim() });
    });

    function displayRawResponse(data) {
        var html = '';
        if (data.txHex) {
            html += '<div class="raw-resp-rx">TX: ' + escapeHtml(data.txHex) + '</div>';
        }
        if (data.rxHex) {
            html += '<div class="raw-resp-rx" style="margin-top:4px;">RX: ' + escapeHtml(data.rxHex) + '</div>';
        }
        if (data.parsed) {
            html += '<div class="raw-resp-body">' + escapeHtml(data.parsed) + '</div>';
        }
        if (!html) {
            html = '<span class="raw-placeholder">No response data</span>';
        }
        rawResponse.innerHTML = html;
    }

    // Quick commands
    var QUICK_COMMANDS = [
        { label: 'Get Brightness (0x10)', hex: '01 10' },
        { label: 'Get Contrast (0x12)', hex: '01 12' },
        { label: 'Get Red Gain (0x16)', hex: '01 16' },
        { label: 'Get Green Gain (0x18)', hex: '01 18' },
        { label: 'Get Blue Gain (0x1A)', hex: '01 1A' },
        { label: 'Get Input Source (0x60)', hex: '01 60' },
        { label: 'Set Brightness=100', hex: '03 10 00 64' },
        { label: 'Set Brightness=50', hex: '03 10 00 32' },
        { label: 'Set Brightness=0', hex: '03 10 00 00' },
        { label: 'Set Contrast=75', hex: '03 12 00 4B' },
        { label: 'Capabilities', hex: 'F3 00 00' }
    ];

    for (var q = 0; q < QUICK_COMMANDS.length; q++) {
        (function (cmd) {
            var btn = document.createElement('button');
            btn.className = 'raw-quick-btn';
            btn.textContent = cmd.label;
            btn.addEventListener('click', function () {
                rawHexInput.value = cmd.hex;
                computeTxHex();
                btnRawSend.click();
            });
            rawQuickList.appendChild(btn);
        })(QUICK_COMMANDS[q]);
    }

    function setStatus(text) {
        statusTextEl.textContent = text;
        clearTimeout(setStatus._timer);
        setStatus._timer = setTimeout(function () {
            statusTextEl.textContent = 'Ready';
        }, 3000);
    }

    // ---- Monitor list ----

    function showScanOverlay() {
        if (!scanOverlayEl) return;
        scanOverlayEl.classList.remove('hidden');
        // Safety net: never let the overlay stick if no response arrives.
        clearTimeout(showScanOverlay._timer);
        showScanOverlay._timer = setTimeout(hideScanOverlay, 8000);
    }

    function hideScanOverlay() {
        if (!scanOverlayEl) return;
        clearTimeout(showScanOverlay._timer);
        scanOverlayEl.classList.add('hidden');
    }

    function refreshMonitors() {
        activeMonitorIndex = -1;
        showScanOverlay();
        sendToHost('enumerateMonitors');
        setStatus('Scanning monitors...');
    }

    function renderMonitorList() {
        monitorListEl.innerHTML = '';
        if (monitors.length === 0) {
            var li = document.createElement('li');
            li.className = 'monitor-item empty';
            li.textContent = 'No monitors detected';
            monitorListEl.appendChild(li);
            return;
        }

        for (var i = 0; i < monitors.length; i++) {
            var li = document.createElement('li');
            li.className = 'monitor-item';
            li.textContent = monitors[i].name;
            li.dataset.index = i;
            li.addEventListener('click', (function (idx) {
                return function () { selectMonitor(idx); };
            })(i));
            if (i === activeMonitorIndex) {
                li.classList.add('active');
            }
            monitorListEl.appendChild(li);
        }
    }

    function selectMonitor(index) {
        activeMonitorIndex = index;

        var items = monitorListEl.querySelectorAll('.monitor-item');
        for (var i = 0; i < items.length; i++) {
            items[i].classList.toggle('active', parseInt(items[i].dataset.index) === index);
        }

        if (index < 0) {
            welcomeEl.classList.add('visible');
            controlsEl.classList.remove('visible');
            return;
        }

        welcomeEl.classList.remove('visible');
        controlsEl.classList.add('visible');

        var mon = monitors[index];
        monitorNameEl.textContent = mon.name;

        // Capabilities-first: read caps, then build controls and query
        // supported codes in the 'capabilities' response handler above.
        currentCapsMap = {};
        vcpControlsEl.innerHTML = '';
        sendToHost('getCapabilities', { monitor: index });
    }

    // ---- Capabilities toggle ----

    capsToggleEl.addEventListener('click', function () {
        var isOpen = capsToggleEl.classList.toggle('open');
        capsContentEl.classList.toggle('hidden', !isOpen);
    });

    // ---- Refresh button ----

    btnRefreshEl.addEventListener('click', refreshMonitors);

    // ---- Tab switching ----

    var tabBtns = document.querySelectorAll('.tab-btn');
    for (var t = 0; t < tabBtns.length; t++) {
        tabBtns[t].addEventListener('click', function (btn) {
            return function () {
                var targetTab = btn.dataset.tab;

                // Update active tab button
                var allBtns = document.querySelectorAll('.tab-btn');
                for (var b = 0; b < allBtns.length; b++) {
                    allBtns[b].classList.toggle('active', allBtns[b] === btn);
                }

                // Show/hide tab content
                tabControls.classList.toggle('hidden', targetTab !== 'controls');
                tabLog.classList.toggle('hidden', targetTab !== 'log');
                tabRaw.classList.toggle('hidden', targetTab !== 'raw');

                if (targetTab === 'log') {
                    renderLogEntries();
                }
                if (targetTab === 'raw') {
                    computeTxHex();
                }
            };
        }(tabBtns[t]));
    }

    // ---- Clear log ----

    btnClearLogEl.addEventListener('click', function () {
        logRecords = [];
        pendingByKey = {};
        logEntriesEl.innerHTML = '';
    });

    // ---- Theme ----

    function applyTheme(name) {
        if (!name || name === 'glass') {
            document.documentElement.removeAttribute('data-theme');
        } else {
            document.documentElement.setAttribute('data-theme', name);
        }
        try { localStorage.setItem('ddcci.theme', name); } catch (e) {}
    }

    (function initTheme() {
        var saved = 'graphite';
        try { saved = localStorage.getItem('ddcci.theme') || 'graphite'; } catch (e) {}
        var sel = document.getElementById('theme-select');
        if (sel) {
            sel.value = saved;
            sel.addEventListener('change', function () { applyTheme(this.value); });
        }
        applyTheme(saved);
    })();

    // ---- Init ----

    var _bridgeReady = false;

    function init() {
        if (window.chrome && window.chrome.webview) {
            if (!_bridgeReady) {
                _bridgeReady = true;
                window.chrome.webview.addEventListener('message', onHostMessage);
            }
            refreshMonitors();
        } else {
            setTimeout(init, 50);
        }
    }
    init();
})();
