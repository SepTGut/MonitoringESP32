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
    const isDemoMode = window.location.hostname.endsWith('.github.io') ||
        window.location.protocol === 'file:' ||
        window.location.search.includes('demo=true');

    // --- Mock Database for Demo Mode (Static Hosting) ---
    let demoConfigStore = {
        apSsid: "ESP32-WIND-MONITOR",
        apPasswordConfigured: true,
        staEnabled: false,
        staSsid: "",
        staPasswordConfigured: false,
        pollMs: 100,
        wsPushMs: 500,
        logMs: 1000,
        zmpt1Cal: 150.0,
        zmpt2Cal: 150.0,
        zmctCal: 5.0,
        pf: 0.85,
        maxV: 80,
        maxA: 30,
        maxAcV: 250,
        maxAcA: 30,
        maxRpm: 3000,
        maxTemp: 100,
        ina1Addr: 64,  // 0x40
        ina2Addr: 65,  // 0x41
        useAds1115: false,
        adsAddr: 72,   // 0x48
        setupRequired: false
    };

    // --- API Fetch Interceptor for static hosting ---
    function apiFetch(url, options) {
        if (isDemoMode) {
            return new Promise((resolve) => {
                setTimeout(() => {
                    if (url === '/api/sysinfo') {
                        resolve({
                            json: () => Promise.resolve({
                                fw: 'v1.1.0-demo',
                                heap: 245100,
                                minHeap: 220000,
                                uptime: Math.floor(performance.now() / 1000),
                                clients: 1,
                                cycleMs: 12,
                                overruns: 0,
                                sensorStackFree: 4096,
                                networkStackFree: 3200,
                                adcMode: demoConfigStore.useAds1115 ? 'ADS1115 (16-bit I2C)' : 'Internal (eFuse Calibrated)',
                                i2c: demoConfigStore.useAds1115 ? [0x27, 0x44, 0x45, demoConfigStore.adsAddr] : [0x27, 0x44, 0x45]
                            })
                        });
                    } else if (url === '/api/config' && options && options.method === 'POST') {
                        const body = JSON.parse(options.body);
                        Object.assign(demoConfigStore, body);
                        resolve({
                            ok: true,
                            json: () => Promise.resolve({ ok: true, restartRequired: false })
                        });
                    } else if (url === '/api/config') {
                        resolve({
                            json: () => Promise.resolve(demoConfigStore)
                        });
                    } else if (url === '/api/restart') {
                        resolve({
                            ok: true,
                            json: () => Promise.resolve({ ok: true })
                        });
                    } else if (url === '/api/i2c-scan' || url === '/api/adc-calibrate') {
                        resolve({
                            ok: true,
                            json: () => Promise.resolve({ ok: true })
                        });
                    }
                }, 150);
            });
        }
        return fetch(url, options);
    }

    // --- Uptime Formatter ---
    function formatUptime(seconds) {
        if (seconds == null || seconds < 0) return '--';
        const d = Math.floor(seconds / 86400);
        const h = Math.floor((seconds % 86400) / 3600);
        const m = Math.floor((seconds % 3600) / 60);
        const s = Math.floor(seconds % 60);
        const parts = [];
        if (d > 0) parts.push(d + 'd');
        if (h > 0 || d > 0) parts.push(h + 'h');
        parts.push(m + 'm');
        parts.push(s + 's');
        return parts.join(' ');
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
        batterySoc: $('val-battery-soc'),
        batteryWh: $('badge-battery-wh'),
        barBatterySoc: $('bar-battery-soc'),
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
        cfgMaxAcV: $('cfg-max-ac-v'),
        cfgMaxAcA: $('cfg-max-ac-a'),
        cfgMaxRpm: $('cfg-max-rpm'),
        cfgMaxT: $('cfg-max-t'),
        cfgIna1Addr: $('cfg-ina1-addr'),
        cfgIna2Addr:   $('cfg-ina2-addr'),
        cfgUseAds1115: $('cfg-use-ads1115'),
        cfgAdsAddr:    $('cfg-ads-addr'),
        cfgDummyMode:  $('cfg-dummy-mode'),
        btnSaveCfg:    $('btn-save-cfg'),
        btnRestart:    $('btn-restart'),
        btnScanI2c:    $('btn-scan-i2c'),
        btnCalibrateAdc: $('btn-calibrate-adc'),
        sysAdcOffsets:   $('sys-adc-offsets'),

        // System info
        sysFw: $('sys-fw'),
        sysAdcMode: $('sys-adc-mode'),
        sysHeap: $('sys-heap'),
        sysMinHeap: $('sys-min-heap'),
        sysUptime: $('sys-uptime'),
        sysClients: $('sys-clients'),
        sysCycleMs: $('sys-cycle-ms'),
        sysOverruns: $('sys-overruns'),
        sysSensorStack: $('sys-sensor-stack'),
        sysNetworkStack: $('sys-network-stack'),
        sysI2cMap: $('sys-i2c-map'),

        // Setup banner
        setupBanner: $('setup-banner'),

        // Health badges
        healthAcV1: $('health-acV1'),
        healthAcI: $('health-acI'),

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
                uptime: Math.floor(performance.now() / 1000),
                powerMode: 'estimated',
                health: { acV1: 1, acV2: 1, acI: 1, ina1: 1, ina2: 1, temp1: 1, temp2: 1, cpuTemp: 1, rpm: 1 },
                cycleMs: 12, overruns: 0
            });
        }, demoConfigStore.wsPushMs || 500);
    }

    function connectWS() {
        if (isDemoMode) {
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

    // --- Sensor Health Indicator Update ---
    function updateHealthIndicators(health) {
        if (!health) return;

        const healthMap = {
            'acV1': health.acV1, 'acV2': health.acV2, 'acI': health.acI,
            'ina1': health.ina1, 'ina2': health.ina2, 'temp1': health.temp1,
            'temp2': health.temp2, 'cpuTemp': health.cpuTemp, 'rpm': health.rpm
        };

        // Update health badges where they exist
        for (const [key, value] of Object.entries(healthMap)) {
            const badge = $('health-' + key);
            if (badge) {
                if (!value) {
                    badge.textContent = '\u26a0';
                    badge.title = 'Sensor disconnected or error';
                    badge.classList.add('unhealthy');
                } else {
                    badge.textContent = '';
                    badge.title = '';
                    badge.classList.remove('unhealthy');
                }
            }
        }

        // Add/remove unhealthy class on metric cards
        document.querySelectorAll('.metric-card[data-sensor]').forEach(card => {
            const sensor = card.dataset.sensor;
            if (sensor && healthMap[sensor] !== undefined) {
                card.classList.toggle('sensor-unhealthy', !healthMap[sensor]);
            }
        });
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

        // Progress bars — AC uses configurable AC limits
        setBar(dom.barAcVolt1, data.acV, cfg.maxACVoltage);
        setBar(dom.barAcCur, data.acA, cfg.maxACCurrent);
        setBar(dom.barAcVolt2, data.acV2, cfg.maxACVoltage);
        // DC uses configurable DC limits
        setBar(dom.barDcVolt1, data.dcV1, cfg.maxVoltage);
        setBar(dom.barDcCur1, data.dcA1, cfg.maxCurrent);
        setBar(dom.barDcVolt2, data.dcV2, cfg.maxVoltage);
        setBar(dom.barDcCur2, data.dcA2, cfg.maxCurrent);
        
        // Battery SoC (12V 68Ah)
        let soc = data.batterySoc;
        let wh = data.batteryWh;
        if (soc == null && data.dcV1 != null) {
            const v = data.dcV1;
            if (v <= 11.85) soc = 0;
            else if (v >= 12.75) soc = 100;
            else {
                const table = [
                    [11.85, 0], [11.95, 10], [12.05, 25], [12.15, 38], [12.25, 50],
                    [12.38, 65], [12.50, 75], [12.62, 88], [12.75, 100]
                ];
                for (let i = 0; i < table.length - 1; i++) {
                    if (v >= table[i][0] && v <= table[i+1][0]) {
                        soc = table[i][1] + ((table[i+1][1] - table[i][1]) / (table[i+1][0] - table[i][0])) * (v - table[i][0]);
                        break;
                    }
                }
            }
            wh = (soc / 100) * 444.60; // 444.60 Wh effective for Lakoni 65Ah @ 57% SoH
        }
        if (soc != null) {
            setText(dom.batterySoc, Math.round(soc).toString());
            if (dom.batteryWh) setText(dom.batteryWh, fmt(wh, 1) + ' Wh');
            if (dom.barBatterySoc) {
                const clampedSoc = Math.max(0, Math.min(100, soc));
                dom.barBatterySoc.style.width = clampedSoc + '%';
                if (clampedSoc >= 70) {
                    dom.barBatterySoc.style.background = 'var(--c-accent, #10b981)';
                } else if (clampedSoc >= 35) {
                    dom.barBatterySoc.style.background = '#f59e0b';
                } else {
                    dom.barBatterySoc.style.background = '#ef4444';
                }
            }
        }

        setBar(dom.barRpm, data.rpm, cfg.maxRPM);
        setBar(dom.barTemp1, data.t1, cfg.maxTemp);
        setBar(dom.barTemp2, data.t2, cfg.maxTemp);
        setBar(dom.barTempEsp, data.tEsp, cfg.maxTemp);

        // Uptime
        if (data.uptime != null) {
            setText(dom.uptimeLabel, 'Uptime: ' + formatUptime(data.uptime));
        }

        // Setup-required banner
        if (data.setupRequired) {
            if (dom.setupBanner) dom.setupBanner.style.display = 'flex';
        }

        // Health indicators
        updateHealthIndicators(data.health);

        // Live diagnostics update in the footer-style area
        if (data.cycleMs != null && dom.sysCycleMs) dom.sysCycleMs.textContent = data.cycleMs + ' ms';
        if (data.overruns != null && dom.sysOverruns) dom.sysOverruns.textContent = data.overruns;
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

    // --- Inline Validation ---
    function validateSettings() {
        const errors = [];

        // AP SSID
        const apSsid = dom.cfgApSsid.value;
        if (apSsid.length < 1 || apSsid.length > 32) {
            errors.push('AP SSID must be 1-32 characters');
        }

        // AP Password (only validate if non-empty)
        const apPass = dom.cfgApPass.value;
        if (apPass && (apPass.length < 8 || apPass.length > 63)) {
            errors.push('AP password must be 8-63 characters');
        }

        // STA Password (only validate if STA is enabled and password is provided)
        const staPass = dom.cfgStaPass.value;
        if (dom.cfgStaEnabled.checked) {
            const staSsid = dom.cfgStaSsid.value;
            if (staSsid.length < 1 || staSsid.length > 32) {
                errors.push('Router SSID must be 1-32 characters when STA is enabled');
            }
            if (staPass && (staPass.length < 8 || staPass.length > 63)) {
                errors.push('Router password must be 8-63 characters');
            }
        }

        // Timing intervals
        const pollMs = parseInt(dom.cfgPollMs.value, 10);
        if (isNaN(pollMs) || pollMs < 100 || pollMs > 60000) {
            errors.push('Sensor poll interval must be 100-60000 ms');
        }
        const wsPushMs = parseInt(dom.cfgPushMs.value, 10);
        if (isNaN(wsPushMs) || wsPushMs < 100 || wsPushMs > 60000) {
            errors.push('WebSocket push interval must be 100-60000 ms');
        }
        const logMs = parseInt(dom.cfgLogMs.value, 10);
        if (isNaN(logMs) || logMs < 250 || logMs > 60000) {
            errors.push('Serial log interval must be 250-60000 ms');
        }

        // Calibration values
        const calFields = [
            { el: dom.cfgCalZmpt1, name: 'ZMPT1 cal', min: 0.01, max: 1000 },
            { el: dom.cfgCalZmpt2, name: 'ZMPT2 cal', min: 0.01, max: 1000 },
            { el: dom.cfgCalZmct, name: 'ZMCT cal', min: 0.01, max: 1000 }
        ];
        for (const f of calFields) {
            const v = parseFloat(f.el.value);
            if (isNaN(v) || !isFinite(v) || v < f.min || v > f.max) {
                errors.push(f.name + ' must be between ' + f.min + ' and ' + f.max);
            }
        }

        // Power factor
        const pf = parseFloat(dom.cfgCalPf.value);
        if (isNaN(pf) || !isFinite(pf) || pf < 0 || pf > 1) {
            errors.push('Power factor must be between 0 and 1');
        }

        // Display limits
        const maxV = parseFloat(dom.cfgMaxV.value);
        if (isNaN(maxV) || maxV < 1) errors.push('Max DC Voltage must be >= 1');
        const maxA = parseFloat(dom.cfgMaxA.value);
        if (isNaN(maxA) || maxA < 0.1) errors.push('Max DC Current must be >= 0.1');
        if (dom.cfgMaxAcV) {
            const maxAcV = parseFloat(dom.cfgMaxAcV.value);
            if (isNaN(maxAcV) || maxAcV < 1) errors.push('Max AC Voltage must be >= 1');
        }
        if (dom.cfgMaxAcA) {
            const maxAcA = parseFloat(dom.cfgMaxAcA.value);
            if (isNaN(maxAcA) || maxAcA < 0.1) errors.push('Max AC Current must be >= 0.1');
        }

        // INA226 addresses must be distinct
        const ina1 = parseInt(dom.cfgIna1Addr.value, 10);
        const ina2 = parseInt(dom.cfgIna2Addr.value, 10);
        if (ina1 === ina2) {
            errors.push('INA226 addresses must be distinct');
        }

        return errors;
    }

    // --- Settings: Load Config from ESP32 ---
    function loadConfig() {
        apiFetch('/api/config')
            .then(r => r.json())
            .then(data => {
                if (data.apSsid != null) dom.cfgApSsid.value = data.apSsid;
                dom.cfgApPass.value = '';
                dom.cfgApPass.placeholder = data.apPasswordConfigured ? 'Configured (leave blank to keep)' : 'Set AP password';
                if (data.staEnabled != null) dom.cfgStaEnabled.checked = data.staEnabled;
                if (data.staSsid != null) dom.cfgStaSsid.value = data.staSsid;
                dom.cfgStaPass.value = '';
                dom.cfgStaPass.placeholder = data.staPasswordConfigured ? 'Configured (leave blank to keep)' : 'Set router password';

                if (data.setupRequired) {
                    showToast('Setup required: replace the temporary AP password before saving other settings.', 'error');
                    if (dom.setupBanner) dom.setupBanner.style.display = 'flex';
                    if (dom.btnRestart) dom.btnRestart.disabled = true;
                } else {
                    if (dom.setupBanner) dom.setupBanner.style.display = 'none';
                    if (dom.btnRestart) dom.btnRestart.disabled = false;
                }

                if (data.pollMs != null) dom.cfgPollMs.value = data.pollMs;
                if (data.wsPushMs != null) dom.cfgPushMs.value = data.wsPushMs;
                if (data.logMs != null) dom.cfgLogMs.value = data.logMs;

                if (data.zmpt1Cal != null) dom.cfgCalZmpt1.value = data.zmpt1Cal;
                if (data.zmpt2Cal != null) dom.cfgCalZmpt2.value = data.zmpt2Cal;
                if (data.zmctCal != null) dom.cfgCalZmct.value = data.zmctCal;
                if (data.pf != null) dom.cfgCalPf.value = data.pf;

                if (data.maxV != null) { dom.cfgMaxV.value = data.maxV; cfg.maxVoltage = data.maxV; }
                if (data.maxA != null) { dom.cfgMaxA.value = data.maxA; cfg.maxCurrent = data.maxA; }
                if (data.maxAcV != null && dom.cfgMaxAcV) { dom.cfgMaxAcV.value = data.maxAcV; cfg.maxACVoltage = data.maxAcV; }
                if (data.maxAcA != null && dom.cfgMaxAcA) { dom.cfgMaxAcA.value = data.maxAcA; cfg.maxACCurrent = data.maxAcA; }
                if (data.maxRpm != null) { dom.cfgMaxRpm.value = data.maxRpm; cfg.maxRPM = data.maxRpm; }
                if (data.maxTemp != null) { dom.cfgMaxT.value = data.maxTemp; cfg.maxTemp = data.maxTemp; }

                if (data.ina1Addr != null) dom.cfgIna1Addr.value = data.ina1Addr;
                if (data.ina2Addr != null) dom.cfgIna2Addr.value = data.ina2Addr;
                if (data.useAds1115 != null && dom.cfgUseAds1115) dom.cfgUseAds1115.checked = data.useAds1115;
                if (data.adsAddr != null && dom.cfgAdsAddr) dom.cfgAdsAddr.value = data.adsAddr;
                if (data.dummyMode != null && dom.cfgDummyMode) dom.cfgDummyMode.checked = data.dummyMode;

                if (dom.sysAdcOffsets && data.zmpt1OffsetMv != null) {
                    dom.sysAdcOffsets.textContent = `Z1: ${data.zmpt1OffsetMv.toFixed(0)}mV | Z2: ${data.zmpt2OffsetMv.toFixed(0)}mV | I: ${data.zmctOffsetMv.toFixed(0)}mV`;
                }
            })
            .catch(() => showToast('Failed to load settings', 'error'));
    }

    // --- Settings: Load System Info ---
    function loadSysInfo() {
        apiFetch('/api/sysinfo')
            .then(r => r.json())
            .then(data => {
                if (data.fw != null) dom.sysFw.textContent = data.fw;
                if (data.adcMode != null && dom.sysAdcMode) dom.sysAdcMode.textContent = data.adcMode;
                if (data.heap != null) dom.sysHeap.textContent = (data.heap / 1024).toFixed(1) + ' KB';
                if (data.minHeap != null && dom.sysMinHeap) dom.sysMinHeap.textContent = (data.minHeap / 1024).toFixed(1) + ' KB';
                if (data.uptime != null) dom.sysUptime.textContent = formatUptime(data.uptime);
                if (data.clients != null) dom.sysClients.textContent = data.clients;
                if (data.cycleMs != null && dom.sysCycleMs) dom.sysCycleMs.textContent = data.cycleMs + ' ms';
                if (data.overruns != null && dom.sysOverruns) dom.sysOverruns.textContent = data.overruns;
                if (data.sensorStackFree != null && dom.sysSensorStack) dom.sysSensorStack.textContent = data.sensorStackFree + ' B';
                if (data.networkStackFree != null && dom.sysNetworkStack) dom.sysNetworkStack.textContent = data.networkStackFree + ' B';

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
        // Client-side validation
        const errors = validateSettings();
        if (errors.length > 0) {
            showToast(errors[0], 'error');
            return;
        }

        const payload = {
            apSsid: dom.cfgApSsid.value,
            staEnabled: dom.cfgStaEnabled.checked,
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
            useAds1115: dom.cfgUseAds1115 ? dom.cfgUseAds1115.checked : false,
            adsAddr: dom.cfgAdsAddr ? parseInt(dom.cfgAdsAddr.value, 10) : 72,
            dummyMode: dom.cfgDummyMode ? dom.cfgDummyMode.checked : false
        };
        // Include AC display limits & optional credentials
        if (dom.cfgMaxAcV) payload.maxAcV = parseFloat(dom.cfgMaxAcV.value);
        if (dom.cfgMaxAcA) payload.maxAcA = parseFloat(dom.cfgMaxAcA.value);
        if (dom.cfgStaSsid.value) payload.staSsid = dom.cfgStaSsid.value;
        if (dom.cfgApPass.value) payload.apPass = dom.cfgApPass.value;
        if (dom.cfgStaPass.value) payload.staPass = dom.cfgStaPass.value;

        apiFetch('/api/config', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(payload)
        })
            .then(async r => {
                const body = await r.json();
                if (r.ok === false) throw new Error(body.error || 'Configuration rejected');
                return body;
            })
            .then(data => {
                if (data.ok) {
                    if (data.restartRequired) {
                        showToast('Settings saved \u2014 restart required to apply WiFi or INA address changes.', 'success');
                    } else {
                        showToast('Settings saved successfully!', 'success');
                    }
                    // Update local visual limit settings dynamically
                    cfg.maxVoltage = payload.maxV;
                    cfg.maxCurrent = payload.maxA;
                    if (payload.maxAcV) cfg.maxACVoltage = payload.maxAcV;
                    if (payload.maxAcA) cfg.maxACCurrent = payload.maxAcA;
                    cfg.maxRPM = payload.maxRpm;
                    cfg.maxTemp = payload.maxTemp;
                    // Reload config to update setup state
                    loadConfig();
                } else {
                    showToast('Failed to save: ' + (data.error || 'Unknown error'), 'error');
                }
            })
            .catch(error => showToast(error.message || 'Failed to save configuration', 'error'));
    });

    // --- Settings: Restart ---
    dom.btnRestart.addEventListener('click', () => {
        if (!confirm('Are you sure you want to reboot the device?')) return;

        apiFetch('/api/restart', { method: 'POST' })
            .then(async r => {
                const body = await r.json();
                if (body.ok === false) throw new Error(body.error || 'Restart failed');
                return body;
            })
            .then(data => {
                if (data.ok) {
                    showToast('Device restarting...', 'success');
                    setTimeout(() => window.location.reload(), 4000);
                } else {
                    showToast('Failed to restart device', 'error');
                }
            })
            .catch(err => showToast(err.message || 'Device reboot request failed', 'error'));
    });

    // --- Settings: Scan I2C Bus ---
    if (dom.btnScanI2c) {
        dom.btnScanI2c.addEventListener('click', () => {
            dom.btnScanI2c.disabled = true;
            dom.btnScanI2c.textContent = 'Scanning I2C Bus...';
            apiFetch('/api/i2c-scan', { method: 'POST' })
                .then(r => r.json())
                .then(() => {
                    showToast('I2C Scan requested', 'info');
                    setTimeout(() => {
                        loadSysInfo();
                        dom.btnScanI2c.disabled = false;
                        dom.btnScanI2c.textContent = 'Scan I2C Bus';
                    }, 1000);
                })
                .catch(() => {
                    showToast('Failed to scan I2C bus', 'error');
                    dom.btnScanI2c.disabled = false;
                    dom.btnScanI2c.textContent = 'Scan I2C Bus';
                });
        });
    }

    if (dom.btnCalibrateAdc) {
        dom.btnCalibrateAdc.addEventListener('click', () => {
            dom.btnCalibrateAdc.disabled = true;
            dom.btnCalibrateAdc.textContent = 'Calibrating Baseline (1s)...';
            apiFetch('/api/adc-calibrate', { method: 'POST' })
                .then(r => r.json())
                .then(() => {
                    showToast('ADC Zero Baseline Calibrated & Saved!', 'success');
                    setTimeout(() => {
                        loadConfig();
                        dom.btnCalibrateAdc.disabled = false;
                        dom.btnCalibrateAdc.textContent = 'Calibrate ADC Zero-Point (0V / 0A)';
                    }, 1200);
                })
                .catch(() => {
                    showToast('ADC Calibration failed', 'error');
                    dom.btnCalibrateAdc.disabled = false;
                    dom.btnCalibrateAdc.textContent = 'Calibrate ADC Zero-Point (0V / 0A)';
                });
        });
    }

    // --- Initialize ---
    connectWS();

})();
