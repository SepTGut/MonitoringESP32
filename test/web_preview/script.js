/* ============================================
   Wind Generator Monitor — Application Logic
   WebSocket + Demo Mode + Dashboard Updates
   ============================================ */

(function () {
    'use strict';

    // --- Display limits for progress bars ---
    const cfg = {
        maxVoltage: 80,
        maxCurrent: 30,
        maxACVoltage: 250,
        maxACCurrent: 30,
        maxRPM: 3000,
        maxTemp: 100
    };

    // --- WebSocket ---
    let targetIP = window.location.hostname;
    if (targetIP === '127.0.0.1' || targetIP === 'localhost') {
        targetIP = '192.168.4.1'; // Connect to ESP32 SoftAP IP by default when previewing locally
    }
    const urlParams = new URLSearchParams(window.location.search);
    if (urlParams.has('ip')) {
        targetIP = urlParams.get('ip');
    }
    const gateway = `ws://${targetIP}/ws`;
    let ws = null;
    let wsReconnectTimer = null;

    // --- Demo Mode Detection ---
    const isDemoMode = true; // Always enable demo mode in the test preview folder

    // --- Mock Database for Demo Mode (Static Hosting) ---
    let demoConfigStore = {
        apSsid: "ESP32-WIND-MONITOR",
        apPass: "12345678",
        staEnabled: false,
        staSsid: "",
        staPass: "",
        pollMs: 100,
        wsPushMs: 500,
        logMs: 1000,
        zmpt1Cal: 150.0,
        zmpt2Cal: 150.0,
        zmctCal: 5.0,
        pf: 0.85,
        maxV: 80,
        maxA: 30,
        maxRpm: 3000,
        maxTemp: 100,
        ina1Addr: 64,  // 0x40
        ina2Addr: 65   // 0x41
    };

    // --- API Fetch Interceptor for static hosting ---
    function apiFetch(url, options) {
        if (isDemoMode) {
            return new Promise((resolve) => {
                setTimeout(() => {
                    if (url === '/api/sysinfo') {
                        resolve({
                            json: () => Promise.resolve({
                                fw: 'v1.0.0-demo',
                                heap: 245100,
                                uptime: Math.floor(performance.now() / 1000),
                                clients: 1,
                                i2c: [0x27, 0x40, 0x41]
                            })
                        });
                    } else if (url === '/api/config' && options && options.method === 'POST') {
                        const body = JSON.parse(options.body);
                        Object.assign(demoConfigStore, body);
                        resolve({
                            json: () => Promise.resolve({ ok: true })
                        });
                    } else if (url === '/api/config') {
                        resolve({
                            json: () => Promise.resolve(demoConfigStore)
                        });
                    } else if (url === '/api/restart') {
                        resolve({
                            json: () => Promise.resolve({ ok: true })
                        });
                    } else if (url === '/api/i2c-scan') {
                        resolve({
                            json: () => Promise.resolve({ ok: true })
                        });
                    }
                }, 150);
            });
        }
        return fetch(url, options);
    }

    // --- DOM Cache ---
    const $ = (id) => document.getElementById(id);

    const dom = {
        // Power hero values
        acPwr: $('val-acpwr'),
        dcPwr1: $('val-dcpwr1'),
        dcPwr2: $('val-dcpwr2'),

        // Power rings
        ringAC: $('power-ring-ac'),
        ringDC1: $('power-ring-dc1'),
        ringDC2: $('power-ring-dc2'),

        // Metric values
        acVolt1: $('val-acvolt1'),
        acCur: $('val-accur'),
        acVolt2: $('val-acvolt2'),
        dcVolt1: $('val-dcvolt1'),
        dcCur1: $('val-dccur1'),
        dcVolt2: $('val-dcvolt2'),
        dcCur2: $('val-dccur2'),
        rpm: $('val-rpm'),
        temp1: $('val-temp1'),
        temp2: $('val-temp2'),
        tempEsp: $('val-temp-esp'),

        // Progress bars
        barAcVolt1: $('bar-acvolt1'),
        barAcCur: $('bar-accur'),
        barAcVolt2: $('bar-acvolt2'),
        barDcVolt1: $('bar-dcvolt1'),
        barDcCur1: $('bar-dccur1'),
        barDcVolt2: $('bar-dcvolt2'),
        barDcCur2: $('bar-dccur2'),
        barRpm: $('bar-rpm'),
        barTemp1: $('bar-temp1'),
        barTemp2: $('bar-temp2'),
        barTempEsp: $('bar-temp-esp'),

        // Connection status
        wsDot: $('ws-dot'),
        wsLabel: $('ws-label'),
        wsDotMobile: $('ws-dot-mobile'),

        // Uptime
        uptimeLabel: $('uptime-label'),

        // Navigation
        navDashboard: $('nav-dashboard'),
        navSettings: $('nav-settings'),
        pageDashboard: $('page-dashboard'),
        pageSettings: $('page-settings'),

        // Mobile
        hamburger: $('hamburger'),
        sidebar: $('sidebar'),

        // Settings Form Controls
        cfgApSsid: $('cfg-ap-ssid'),
        cfgApPass: $('cfg-ap-pass'),
        cfgStaEnabled: $('cfg-sta-enabled'),
        cfgStaSsid: $('cfg-sta-ssid'),
        cfgStaPass: $('cfg-sta-pass'),
        cfgPollMs: $('cfg-poll-ms'),
        cfgPushMs: $('cfg-push-ms'),
        cfgLogMs: $('cfg-log-ms'),
        cfgCalZmpt1: $('cfg-cal-zmpt1'),
        cfgCalZmpt2: $('cfg-cal-zmpt2'),
        cfgCalZmct: $('cfg-cal-zmct'),
        cfgCalPf: $('cfg-cal-pf'),
        cfgMaxV: $('cfg-max-v'),
        cfgMaxA: $('cfg-max-a'),
        cfgMaxRpm: $('cfg-max-rpm'),
        cfgMaxT: $('cfg-max-t'),
        cfgIna1Addr: $('cfg-ina1-addr'),
        cfgIna2Addr:   $('cfg-ina2-addr'),
        cfgDummyMode:  $('cfg-dummy-mode'),
        btnSaveCfg:    $('btn-save-cfg'),
        btnRestart:    $('btn-restart'),
        btnScanI2c:    $('btn-scan-i2c'),

        // System info
        sysFw: $('sys-fw'),
        sysHeap: $('sys-heap'),
        sysUptime: $('sys-uptime'),
        sysClients: $('sys-clients'),
        sysI2cMap: $('sys-i2c-map'),

        // Virtual LCD
        lcdLine0:       $('lcd-line-0'),
        lcdLine1:       $('lcd-line-1'),
        lcdScreenTitle: $('lcd-screen-title'),
        lcdDot:         $('lcd-dot'),

        // Toast
        toast: $('toast'),
        toastMsg: $('toast-msg')
    };

    // --- Navigation ---
    function switchPage(page) {
        document.querySelectorAll('.page').forEach(p => p.classList.remove('active'));
        document.querySelectorAll('.nav-btn').forEach(b => b.classList.remove('active'));

        const target = $('page-' + page);
        const btn = $('nav-' + page);
        if (target) target.classList.add('active');
        if (btn) btn.classList.add('active');

        // Close mobile sidebar
        dom.sidebar.classList.remove('open');

        if (page === 'settings') {
            loadSysInfo();
            loadConfig();
        }
    }

    dom.navDashboard.addEventListener('click', () => switchPage('dashboard'));
    dom.navSettings.addEventListener('click', () => switchPage('settings'));

    // Mobile hamburger
    dom.hamburger.addEventListener('click', () => {
        dom.sidebar.classList.toggle('open');
    });

    // Close sidebar on backdrop click (mobile)
    document.addEventListener('click', (e) => {
        if (window.innerWidth <= 768 &&
            dom.sidebar.classList.contains('open') &&
            !dom.sidebar.contains(e.target) &&
            !dom.hamburger.contains(e.target)) {
            dom.sidebar.classList.remove('open');
        }
    });

    // --- Toast ---
    let toastTimer = null;
    function showToast(msg, type) {
        dom.toastMsg.textContent = msg;
        dom.toast.className = 'toast show ' + (type || '');
        clearTimeout(toastTimer);
        toastTimer = setTimeout(() => {
            dom.toast.className = 'toast';
        }, 3000);
    }

    // --- WebSocket Connection ---
    let demoInterval = null;
    let simStep = 0;

    function startDemoSimulation() {
        if (demoInterval) return;
        setConnectionStatus(true);
        if (dom.wsLabel) dom.wsLabel.textContent = 'Live (Demo)';

        demoInterval = setInterval(() => {
            simStep += 0.08;

            const windSpeed = 5.0 + 3.0 * Math.sin(simStep * 0.3);

            const acV = 30.0 + 25.0 * Math.sin(simStep * 0.5) * (windSpeed / 8.0);
            const acA = 2.0 + 1.5 * Math.sin(simStep * 0.7) * (windSpeed / 8.0);
            const acP = Math.max(0, acV * acA * (demoConfigStore.pf || 0.85));
            const acV2 = acV * 0.95 + 1.5 * Math.sin(simStep * 1.1);

            const dcV1 = 12.0 + 2.0 * Math.sin(simStep * 0.4);
            const dcA1 = 3.0 + 2.0 * Math.sin(simStep * 0.6);
            const dcP1 = Math.max(0, dcV1 * dcA1);

            const dcV2 = 24.0 + 3.0 * Math.sin(simStep * 0.35);
            const dcA2 = 1.5 + 1.0 * Math.sin(simStep * 0.55);
            const dcP2 = Math.max(0, dcV2 * dcA2);

            const rpm = Math.max(0, 800 + 600 * Math.sin(simStep * 0.25) * (windSpeed / 8.0));
            const t1 = 32.0 + 4.0 * Math.sin(simStep * 0.15);
            const t2 = 26.0 + 2.0 * Math.sin(simStep * 0.1);
            const tEsp = 45.0 + 5.0 * Math.sin(simStep * 0.2);

            updateDashboard({
                acV: acV, acA: Math.max(0, acA), acP: acP, acV2: acV2,
                dcV1: dcV1, dcA1: Math.max(0, dcA1), dcP1: dcP1,
                dcV2: dcV2, dcA2: Math.max(0, dcA2), dcP2: dcP2,
                rpm: rpm, t1: t1, t2: t2, tEsp: tEsp,
                uptime: Math.floor(performance.now() / 1000)
            });
        }, demoConfigStore.wsPushMs || 500);
    }

    function connectWS() {
        if (isDemoMode) {
            const cardLcd = document.querySelector('.card-lcd');
            if (cardLcd) cardLcd.style.display = 'flex';
            startDemoSimulation();
            return;
        }

        if (ws && ws.readyState === WebSocket.OPEN) return;

        ws = new WebSocket(gateway);

        ws.onopen = () => {
            setConnectionStatus(true);
            clearTimeout(wsReconnectTimer);
        };

        ws.onclose = () => {
            setConnectionStatus(false);
            wsReconnectTimer = setTimeout(connectWS, 2000);
        };

        ws.onerror = () => { ws.close(); };

        ws.onmessage = (event) => {
            try {
                const data = JSON.parse(event.data);
                updateDashboard(data);
            } catch (e) {
                console.error('JSON parse error:', e);
            }
        };
    }

    function setConnectionStatus(connected) {
        [dom.wsDot, dom.wsDotMobile].forEach(dot => {
            if (dot) dot.classList.toggle('live', connected);
        });
        if (dom.wsLabel) {
            dom.wsLabel.textContent = connected ? 'Live' : 'Offline';
        }
    }

    // --- Dashboard Update ---
    function updateDashboard(data) {
        const acP = data.acP != null ? data.acP : 0;
        const dcP1 = data.dcP1 != null ? data.dcP1 : 0;
        const dcP2 = data.dcP2 != null ? data.dcP2 : 0;

        // Hero power values
        setText(dom.acPwr, acP.toFixed(1));
        setText(dom.dcPwr1, dcP1.toFixed(1));
        setText(dom.dcPwr2, dcP2.toFixed(1));

        // Power rings
        const maxDCPower = cfg.maxVoltage * cfg.maxCurrent;
        const maxACPower = cfg.maxACVoltage * cfg.maxACCurrent;
        setRing(dom.ringAC, acP, maxACPower);
        setRing(dom.ringDC1, dcP1, maxDCPower);
        setRing(dom.ringDC2, dcP2, maxDCPower);

        // Metric values
        setText(dom.acVolt1, fmt(data.acV, 1));
        setText(dom.acCur, fmt(data.acA, 2));
        setText(dom.acVolt2, fmt(data.acV2, 1));
        setText(dom.dcVolt1, fmt(data.dcV1, 2));
        setText(dom.dcCur1, fmt(data.dcA1, 2));
        setText(dom.dcVolt2, fmt(data.dcV2, 2));
        setText(dom.dcCur2, fmt(data.dcA2, 2));
        setText(dom.rpm, data.rpm != null ? Math.round(data.rpm).toString() : '0');
        setText(dom.temp1, fmt(data.t1, 1));
        setText(dom.temp2, fmt(data.t2, 1));
        setText(dom.tempEsp, fmt(data.tEsp, 1));

        // Progress bars
        setBar(dom.barAcVolt1, data.acV, cfg.maxACVoltage);
        setBar(dom.barAcCur, data.acA, cfg.maxACCurrent);
        setBar(dom.barAcVolt2, data.acV2, cfg.maxACVoltage);
        setBar(dom.barDcVolt1, data.dcV1, cfg.maxVoltage);
        setBar(dom.barDcCur1, data.dcA1, cfg.maxCurrent);
        setBar(dom.barDcVolt2, data.dcV2, cfg.maxVoltage);
        setBar(dom.barDcCur2, data.dcA2, cfg.maxCurrent);
        setBar(dom.barRpm, data.rpm, cfg.maxRPM);
        setBar(dom.barTemp1, data.t1, cfg.maxTemp);
        setBar(dom.barTemp2, data.t2, cfg.maxTemp);
        setBar(dom.barTempEsp, data.tEsp, cfg.maxTemp);

        // Uptime
        if (data.uptime != null) {
            setText(dom.uptimeLabel, 'Uptime: ' + formatUptime(data.uptime));
        }

        // Update virtual LCD display to match dynamic hardware outputs
        updateVirtualLcd(data);
    }

    // --- Virtual LCD Preview Simulation ---
    let lcdCurrentScreen = 0;
    let lastLcdRotationTime = Date.now();

    function padLcd(line) {
        line = line.substring(0, 16);
        while (line.length < 16) {
            line += " ";
        }
        return line.toUpperCase();
    }

    function updateVirtualLcd(sensorData) {
        if (!dom.lcdLine0 || !dom.lcdLine1) return;

        const now = Date.now();
        if (now - lastLcdRotationTime >= 3000) {
            lcdCurrentScreen = (lcdCurrentScreen + 1) % 4;
            lastLcdRotationTime = now;
        }

        let l0 = "";
        let l1 = "";
        let title = "";

        const propChars = ['|', '/', '-', '\\'];
        const animFrame = propChars[Math.floor(Date.now() / 100) % 4];

        switch (lcdCurrentScreen) {
            case 0:
                title = "AC OVERVIEW";
                const acV = sensorData.acV != null ? sensorData.acV.toFixed(1) : "0.0";
                const acA = sensorData.acA != null ? sensorData.acA.toFixed(2) : "0.00";
                l0 = `⚡AC:${acV.padStart(5)}V ${acA.padStart(5)}A`;
                
                const acP = sensorData.acP != null ? Math.round(sensorData.acP).toString() : "0";
                const pf = dom.cfgCalPf && dom.cfgCalPf.value ? parseFloat(dom.cfgCalPf.value).toFixed(2) : "0.85";
                l1 = `Pwr:${acP.padStart(4)}W PF${pf}`;
                break;

            case 1:
                title = "DC CHANNELS";
                const dcV1 = sensorData.dcV1 != null ? sensorData.dcV1.toFixed(2) : "0.00";
                const dcA1 = sensorData.dcA1 != null ? sensorData.dcA1.toFixed(2) : "0.00";
                l0 = `🔋D1:${dcV1.padStart(5)}V ${dcA1.padStart(5)}A`;

                const dcV2 = sensorData.dcV2 != null ? sensorData.dcV2.toFixed(2) : "0.00";
                const dcA2 = sensorData.dcA2 != null ? sensorData.dcA2.toFixed(2) : "0.00";
                l1 = `🔋D2:${dcV2.padStart(5)}V ${dcA2.padStart(5)}A`;
                break;

            case 2:
                title = "SPEED & TEMPS";
                const rpm = sensorData.rpm != null ? Math.round(sensorData.rpm) : 0;
                const tEsp = sensorData.tEsp != null ? Math.round(sensorData.tEsp) : 0;
                l0 = `${animFrame} ${rpm.toString().padStart(4)} RPM`;

                const t1 = sensorData.t1 != null ? Math.round(sensorData.t1).toString() : "0";
                const t2 = sensorData.t2 != null ? Math.round(sensorData.t2).toString() : "0";
                l1 = `🌡️${tEsp}C Ext:${t1}/${t2}C`;
                break;

            case 3:
                title = "SYSTEM INFO";
                const ssid = dom.cfgApSsid && dom.cfgApSsid.value ? dom.cfgApSsid.value : "WINDMONITOR";
                l0 = `WiFi: ${ssid}`;
                l1 = "IP  : 192.168.4.1";
                break;
        }

        dom.lcdLine0.textContent = padLcd(l0);
        dom.lcdLine1.textContent = padLcd(l1);
        dom.lcdScreenTitle.textContent = title;
        dom.lcdDot.textContent = "● ACTIVE";
    }

    // --- Helpers ---
    function fmt(val, decimals) {
        return val != null ? val.toFixed(decimals) : (decimals === 1 ? '0.0' : '0.00');
    }

    function setText(el, val) {
        if (el && el.textContent !== val) el.textContent = val;
    }

    function setBar(el, value, max) {
        if (!el) return;
        const pct = Math.min(100, Math.max(0, ((value || 0) / max) * 100));
        el.style.width = pct + '%';
    }

    function setRing(ringEl, power, maxPower) {
        if (!ringEl) return;
        const pct = Math.min(1, Math.max(0, power / maxPower));
        const circumference = 326.7; // 2 × PI × 52
        ringEl.style.strokeDashoffset = circumference * (1 - pct);
    }

    // --- Settings: Load Config from ESP32 ---
    function loadConfig() {
        apiFetch('/api/config')
            .then(r => r.json())
            .then(data => {
                if (data.apSsid != null) dom.cfgApSsid.value = data.apSsid;
                if (data.apPass != null) dom.cfgApPass.value = data.apPass;
                if (data.staEnabled != null) dom.cfgStaEnabled.checked = data.staEnabled;
                if (data.staSsid != null) dom.cfgStaSsid.value = data.staSsid;
                if (data.staPass != null) dom.cfgStaPass.value = data.staPass;

                if (data.pollMs != null) dom.cfgPollMs.value = data.pollMs;
                if (data.wsPushMs != null) dom.cfgPushMs.value = data.wsPushMs;
                if (data.logMs != null) dom.cfgLogMs.value = data.logMs;

                if (data.zmpt1Cal != null) dom.cfgCalZmpt1.value = data.zmpt1Cal;
                if (data.zmpt2Cal != null) dom.cfgCalZmpt2.value = data.zmpt2Cal;
                if (data.zmctCal != null) dom.cfgCalZmct.value = data.zmctCal;
                if (data.pf != null) dom.cfgCalPf.value = data.pf;

                if (data.maxV != null) { dom.cfgMaxV.value = data.maxV; cfg.maxVoltage = data.maxV; }
                if (data.maxA != null) { dom.cfgMaxA.value = data.maxA; cfg.maxCurrent = data.maxA; }
                if (data.maxRpm != null) { dom.cfgMaxRpm.value = data.maxRpm; cfg.maxRPM = data.maxRpm; }
                if (data.maxTemp != null) { dom.cfgMaxT.value = data.maxTemp; cfg.maxTemp = data.maxTemp; }

                if (data.ina1Addr != null) dom.cfgIna1Addr.value = data.ina1Addr;
                if (data.ina2Addr != null) dom.cfgIna2Addr.value = data.ina2Addr;
                if (data.dummyMode != null && dom.cfgDummyMode) dom.cfgDummyMode.checked = data.dummyMode;
            })
            .catch(() => showToast('Failed to load settings', 'error'));
    }

    // --- Settings: Load System Info ---
    function loadSysInfo() {
        apiFetch('/api/sysinfo')
            .then(r => r.json())
            .then(data => {
                if (data.fw != null) dom.sysFw.textContent = data.fw;
                if (data.heap != null) dom.sysHeap.textContent = (data.heap / 1024).toFixed(1) + ' KB';
                if (data.uptime != null) dom.sysUptime.textContent = formatUptime(data.uptime);
                if (data.clients != null) dom.sysClients.textContent = data.clients;

                if (data.i2c != null && Array.isArray(data.i2c)) {
                    dom.sysI2cMap.textContent = data.i2c.length > 0
                        ? data.i2c.map(addr => '0x' + addr.toString(16).toUpperCase()).join(', ')
                        : 'None';
                }
            })
            .catch(() => { });
    }

    // --- Settings: Save Config ---
    dom.btnSaveCfg.addEventListener('click', () => {
        const payload = {
            apSsid: dom.cfgApSsid.value,
            apPass: dom.cfgApPass.value,
            staEnabled: dom.cfgStaEnabled.checked,
            staSsid: dom.cfgStaSsid.value,
            staPass: dom.cfgStaPass.value,
            pollMs: parseInt(dom.cfgPollMs.value, 10),
            wsPushMs: parseInt(dom.cfgPushMs.value, 10),
            logMs: parseInt(dom.cfgLogMs.value, 10),
            zmpt1Cal: parseFloat(dom.cfgCalZmpt1.value),
            zmpt2Cal: parseFloat(dom.cfgCalZmpt2.value),
            zmctCal: parseFloat(dom.cfgCalZmct.value),
            pf: parseFloat(dom.cfgCalPf.value),
            maxV: parseFloat(dom.cfgMaxV.value),
            maxA: parseFloat(dom.cfgMaxA.value),
            maxRpm: parseInt(dom.cfgMaxRpm.value, 10),
            maxTemp: parseInt(dom.cfgMaxT.value, 10),
            ina1Addr: parseInt(dom.cfgIna1Addr.value, 10),
            ina2Addr: parseInt(dom.cfgIna2Addr.value, 10),
            dummyMode: dom.cfgDummyMode ? dom.cfgDummyMode.checked : false
        };

        apiFetch('/api/config', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(payload)
        })
            .then(r => r.json())
            .then(data => {
                if (data.ok) {
                    showToast('Settings saved successfully!', 'success');
                    // Update local visual limit settings dynamically
                    cfg.maxVoltage = payload.maxV;
                    cfg.maxCurrent = payload.maxA;
                    cfg.maxRPM = payload.maxRpm;
                    cfg.maxTemp = payload.maxTemp;
                } else {
                    showToast('Failed to save: ' + (data.error || 'Unknown error'), 'error');
                }
            })
            .catch(() => showToast('Failed to save configuration', 'error'));
    });

    // --- Settings: Restart ---
    dom.btnRestart.addEventListener('click', () => {
        if (!confirm('Are you sure you want to reboot the device?')) return;

        apiFetch('/api/restart', { method: 'POST' })
            .then(r => r.json())
            .then(data => {
                if (data.ok) {
                    showToast('Device restarting...', 'success');
                    setTimeout(() => window.location.reload(), 4000);
                } else {
                    showToast('Failed to restart device', 'error');
                }
            })
            .catch(() => showToast('Device reboot request sent', 'success'));
    });

    // --- Settings: Scan I2C Bus ---
    dom.btnScanI2c.addEventListener('click', () => {
        dom.btnScanI2c.disabled = true;
        dom.sysI2cMap.textContent = 'Scanning...';
        showToast('I2C scan started...', 'success');

        apiFetch('/api/i2c-scan', { method: 'POST' })
            .then(r => r.json())
            .then(data => {
                if (data.ok) {
                    setTimeout(() => {
                        loadSysInfo();
                        showToast('I2C scan complete!', 'success');
                        dom.btnScanI2c.disabled = false;
                    }, 600);
                } else {
                    showToast('Failed to trigger I2C scan', 'error');
                    dom.btnScanI2c.disabled = false;
                }
            })
            .catch(() => {
                setTimeout(() => {
                    loadSysInfo();
                    showToast('I2C scan complete (Demo Mode)', 'success');
                    dom.btnScanI2c.disabled = false;
                }, 600);
            });
    });

    // --- Initialize ---
    connectWS();

})();
