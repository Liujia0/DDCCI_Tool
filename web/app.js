(function () {
    'use strict';

    // ---- VCP feature definitions ----

    var INPUT_SOURCES = {
        0x01: 'VGA-1', 0x02: 'VGA-2', 0x03: 'DVI-1', 0x04: 'DVI-2',
        0x05: 'Composite 1', 0x06: 'Composite 2', 0x07: 'S-Video 1',
        0x08: 'S-Video 2', 0x09: 'Tuner 1', 0x0A: 'Tuner 2', 0x0B: 'Tuner 3',
        0x0C: 'Component 1', 0x0D: 'Component 2', 0x0E: 'Component 3',
        0x0F: 'DisplayPort 1', 0x10: 'DisplayPort 2',
        0x11: 'HDMI 1', 0x12: 'HDMI 2',
        0x1A: 'USB-C'
    };

    var SLIDER_FEATURES = [
        { code: 0x10, label: 'Brightness' },
        { code: 0x12, label: 'Contrast' },
        { code: 0x16, label: 'Red Gain' },
        { code: 0x18, label: 'Green Gain' },
        { code: 0x1A, label: 'Blue Gain' }
    ];

    var SELECT_FEATURES = [
        { code: 0x60, label: 'Input Source', options: INPUT_SOURCES }
    ];

    // ---- State ----

    var monitors = [];
    var activeMonitorIndex = -1;
    var supportedVCPSet = {};   // cache: {vcpCode: true} for current monitor
    var pendingGetVCP = {};

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

    // ---- Log ----

    var logRecords = [];
    var MAX_LOG = 500;

    var VCP_NAMES = {
        0x02: 'New Control', 0x04: 'Restore Factory', 0x05: 'Reset Bright/Cont',
        0x06: 'Reset Geometry', 0x08: 'Reset Color', 0x0B: 'Color Temp Inc',
        0x0C: 'Color Temp Req', 0x10: 'Brightness', 0x12: 'Contrast',
        0x14: 'Color Preset', 0x16: 'Red Gain', 0x18: 'Green Gain', 0x1A: 'Blue Gain',
        0x52: 'Active Control', 0x60: 'Input Source', 0x62: 'Audio Volume',
        0x87: 'Audio Treble', 0x8D: 'Audio Mute', 0xAC: 'Power Mode',
        0xAE: 'Auto Setup', 0xB6: 'Display Osd', 0xC0: 'Display Usage Time',
        0xC6: 'Power Control', 0xC8: 'C9 Display Control', 0xC9: 'Display C9',
        0xCA: 'OSD Language', 0xCC: 'OSD Timeout', 0xD6: 'DPMS', 0xDC: 'MagicBright',
        0xDF: 'VCP Version', 0xE1: 'E1', 0xE2: 'E2', 0xE3: 'E3', 0xF7: 'F7'
    };

    function vcpCodeName(code) {
        return VCP_NAMES[code] || ('0x' + code.toString(16).toUpperCase());
    }

    function addLogEntry(dir, op, detail, sendHex, recvHex) {
        var now = new Date();
        var time = ('0' + now.getHours()).slice(-2) + ':'
                 + ('0' + now.getMinutes()).slice(-2) + ':'
                 + ('0' + now.getSeconds()).slice(-2);
        logRecords.push({ time: time, dir: dir, op: op, detail: detail,
                         sendHex: sendHex || '', recvHex: recvHex || '' });
        if (logRecords.length > MAX_LOG) logRecords.shift();
        if (!tabLog.classList.contains('hidden')) {
            renderLogEntries();
        }
    }

    function escapeHtml(s) {
        return s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
    }

    function renderLogEntries() {
        logEntriesEl.innerHTML = '';
        for (var i = 0; i < logRecords.length; i++) {
            var r = logRecords[i];
            var div = document.createElement('div');
            div.className = 'log-entry';

            var html = '<span class="log-time">' + r.time + '</span>'
                + '<span class="log-dir ' + r.dir + '">' + (r.dir === 'send' ? '→' : '←') + '</span>'
                + '<span class="log-op">' + r.op + '</span>'
                + '<span class="log-detail">' + escapeHtml(r.detail) + '</span>';

            if (r.sendHex) {
                html += '<div class="log-hex-row"><span class="log-hex-label">TX:</span>'
                    + '<span class="log-hex">' + escapeHtml(r.sendHex) + '</span></div>';
            }
            if (r.recvHex) {
                html += '<div class="log-hex-row"><span class="log-hex-label">RX:</span>'
                    + '<span class="log-hex">' + escapeHtml(r.recvHex) + '</span></div>';
            }
            div.innerHTML = html;
            logEntriesEl.appendChild(div);
        }
        logEntriesEl.scrollTop = logEntriesEl.scrollHeight;
    }

    function logSend(method, params) {
        var detail = '';
        if (method === 'getVCP' || method === 'setVCP') {
            detail = vcpCodeName(params.vcpCode) + ' (0x' + params.vcpCode.toString(16).toUpperCase() + ')';
            if (method === 'setVCP') detail += ' = ' + params.value;
        } else if (method === 'enumerateMonitors') {
            detail = 'Scan monitors';
        } else if (method === 'getCapabilities') {
            detail = 'Monitor ' + params.monitor;
        }
        addLogEntry('send', method, detail);
    }

    function logRecv(data) {
        var detail = '';
        if (data.type === 'monitorList') {
            detail = (data.monitors ? data.monitors.length : 0) + ' monitor(s)';
        } else if (data.type === 'vcpFeature') {
            detail = vcpCodeName(data.vcpCode) + ' (0x' + data.vcpCode.toString(16).toUpperCase() + ')'
                   + ' = ' + data.current + '/' + data.max;
        } else if (data.type === 'vcpSet') {
            detail = vcpCodeName(data.vcpCode) + ' (0x' + data.vcpCode.toString(16).toUpperCase() + ')'
                   + ' = ' + data.value;
        } else if (data.type === 'capabilities') {
            var preview = (data.capabilities || '').substring(0, 60);
            detail = preview + (data.capabilities && data.capabilities.length > 60 ? '...' : '');
        } else if (data.error) {
            detail = 'ERROR: ' + data.error;
        }
        addLogEntry('recv', data.type || 'response', detail, data.sendHex, data.recvHex);
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
            if (activeMonitorIndex >= monitors.length) {
                selectMonitor(-1);
            } else if (activeMonitorIndex >= 0) {
                selectMonitor(activeMonitorIndex);
            }
            break;

        case 'vcpFeature':
            updateVCPUI(data.vcpCode, data.current, data.max);
            break;

        case 'vcpSet':
            setStatus('Set OK');
            break;

        case 'capabilities':
            capsContentEl.textContent = data.capabilities || '(empty)';
            supportedVCPSet = {};
            if (data.supportedVCP) {
                for (var s = 0; s < data.supportedVCP.length; s++) {
                    supportedVCPSet[data.supportedVCP[s]] = true;
                }
            }
            buildVCPControls();
            queryAllVCPFeatures();
            // Log each segment's send & receive individually.
            if (data.segments) {
                for (var seg = 0; seg < data.segments.length; seg++) {
                    var sdata = data.segments[seg];
                    var label = 'caps[' + seg + ']';
                    if (data.segments.length > 1) label += ' ' + (seg + 1) + '/' + data.segments.length;
                    addLogEntry('send', label, '', sdata.sendHex, '');
                    if (sdata.recvHex) {
                        addLogEntry('recv', label, '', '', sdata.recvHex);
                    } else {
                        addLogEntry('recv', label, '<read failed>', '', '');
                    }
                }
            }
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

    function refreshMonitors() {
        activeMonitorIndex = -1;
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
        // based on vcp(xx xx ...) in the response handler above.
        supportedVCPSet = {};
        vcpControlsEl.innerHTML = '';
        sendToHost('getCapabilities', { monitor: index });
    }

    function onMonitorItemClick(index) {
        // Called from dynamically created items
        selectMonitor(index);
    }

    // ---- VCP controls ----

    function buildVCPControls() {
        vcpControlsEl.innerHTML = '';

        // Slider features — only for VCP codes the monitor supports
        for (var i = 0; i < SLIDER_FEATURES.length; i++) {
            var feat = SLIDER_FEATURES[i];
            if (!supportedVCPSet[feat.code]) continue;

            var row = document.createElement('div');
            row.className = 'vcp-row';

            var label = document.createElement('span');
            label.className = 'vcp-label';
            label.textContent = feat.label;

            var slider = document.createElement('input');
            slider.type = 'range';
            slider.className = 'vcp-slider';
            slider.min = 0;
            slider.max = 100;
            slider.value = 0;
            slider.dataset.vcpCode = feat.code;

            var valSpan = document.createElement('span');
            valSpan.className = 'vcp-value';
            valSpan.id = 'val-' + feat.code;
            valSpan.textContent = '--';

            slider.addEventListener('input', function (code, el) {
                return function () {
                    var v = parseInt(el.value);
                    var span = document.getElementById('val-' + code);
                    if (span) span.textContent = v;
                };
            }(feat.code, slider));

            slider.addEventListener('change', function (code, el) {
                return function () {
                    sendToHost('setVCP', { monitor: activeMonitorIndex, vcpCode: code, value: parseInt(el.value) });
                };
            }(feat.code, slider));

            row.appendChild(label);
            row.appendChild(slider);
            row.appendChild(valSpan);
            vcpControlsEl.appendChild(row);
        }

        // Select features — only for VCP codes the monitor supports
        for (var j = 0; j < SELECT_FEATURES.length; j++) {
            var selFeat = SELECT_FEATURES[j];
            if (!supportedVCPSet[selFeat.code]) continue;

            var row = document.createElement('div');
            row.className = 'vcp-row';

            var label = document.createElement('span');
            label.className = 'vcp-label';
            label.textContent = selFeat.label;

            var select = document.createElement('select');
            select.className = 'vcp-select';
            select.dataset.vcpCode = selFeat.code;

            var placeholder = document.createElement('option');
            placeholder.value = '';
            placeholder.textContent = 'Detecting...';
            select.appendChild(placeholder);

            var codes = Object.keys(selFeat.options);
            for (var k = 0; k < codes.length; k++) {
                var opt = document.createElement('option');
                opt.value = codes[k];
                opt.textContent = selFeat.options[codes[k]];
                select.appendChild(opt);
            }

            select.addEventListener('change', function (code, el) {
                return function () {
                    if (el.value !== '') {
                        sendToHost('setVCP', { monitor: activeMonitorIndex, vcpCode: code, value: parseInt(el.value) });
                    }
                };
            }(selFeat.code, select));

            row.appendChild(label);
            row.appendChild(select);
            vcpControlsEl.appendChild(row);
        }
    }

    function queryAllVCPFeatures() {
        for (var i = 0; i < SLIDER_FEATURES.length; i++) {
            if (supportedVCPSet[SLIDER_FEATURES[i].code]) {
                sendToHost('getVCP', { monitor: activeMonitorIndex, vcpCode: SLIDER_FEATURES[i].code });
            }
        }
        for (var j = 0; j < SELECT_FEATURES.length; j++) {
            if (supportedVCPSet[SELECT_FEATURES[j].code]) {
                sendToHost('getVCP', { monitor: activeMonitorIndex, vcpCode: SELECT_FEATURES[j].code });
            }
        }
    }

    function updateVCPUI(vcpCode, current, max) {
        // Update slider
        var slider = document.querySelector('.vcp-slider[data-vcp-code="' + vcpCode + '"]');
        if (slider) {
            slider.max = max;
            slider.value = current;
            var valSpan = document.getElementById('val-' + vcpCode);
            if (valSpan) {
                valSpan.textContent = current;
            }
        }

        // Update select
        var select = document.querySelector('.vcp-select[data-vcp-code="' + vcpCode + '"]');
        if (select) {
            select.value = String(current);
        }
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
        logEntriesEl.innerHTML = '';
    });

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
