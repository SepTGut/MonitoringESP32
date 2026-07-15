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
    const gateway = `ws://${window.location.hostname}/ws`;
    let ws = null;
    let wsReconnectTimer = null;

    // --- Demo Mode Detection ---
    const isDemoMode = window.location.hostname.endsWith('.github.io') ||
                       window.location.protocol === 'file:' ||
                       window.location.search.includes('demo=true');

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
        maxTemp: 100
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
                                clients: 1
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
        acPwr:     $('val-acpwr'),
        dcPwr1:    $('val-dcpwr1'),
        dcPwr2:    $('val-dcpwr2'),

        // Power rings
        ringAC:    $('power-ring-ac'),
        ringDC1:   $('power-ring-dc1'),
        ringDC2:   $('power-ring-dc2'),

        // Metric values
        acVolt1:   $('val-acvolt1'),
        acCur:     $('val-accur'),
        acVolt2:   $('val-acvolt2'),
        dcVolt1:   $('val-dcvolt1'),
        dcCur1:    $('val-dccur1'),
        dcVolt2:   $('val-dcvolt2'),
        dcCur2:    $('val-dccur2'),
        rpm:       $('val-rpm'),
        temp1:     $('val-temp1'),
        temp2:     $('val-temp2'),

        // Progress bars
        barAcVolt1: $('bar-acvolt1'),
        barAcCur:   $('bar-accur'),
        barAcVolt2: $('bar-acvolt2'),
        barDcVolt1: $('bar-dcvolt1'),
        barDcCur1:  $('bar-dccur1'),
        barDcVolt2: $('bar-dcvolt2'),
        barDcCur2:  $('bar-dccur2'),
        barRpm:     $('bar-rpm'),
        barTemp1:   $('bar-temp1'),
        barTemp2:   $('bar-temp2'),

        // Connection status
        wsDot:       $('ws-dot'),
        wsLabel:     $('ws-label'),
        wsDotMobile: $('ws-dot-mobile'),

        // Uptime
        uptimeLabel: $('uptime-label'),

        // Navigation
        navDashboard:  $('nav-dashboard'),
        navSettings:   $('nav-settings'),
        pageDashboard: $('page-dashboard'),
        pageSettings:  $('page-settings'),

        // Mobile
        hamburger: $('hamburger'),
        sidebar:   $('sidebar'),

        // Settings Form Controls
        cfgApSsid:     $('cfg-ap-ssid'),
        cfgApPass:     $('cfg-ap-pass'),
        cfgStaEnabled: $('cfg-sta-enabled'),
        cfgStaSsid:    $('cfg-sta-ssid'),
        cfgStaPass:    $('cfg-sta-pass'),
        cfgPollMs:     $('cfg-poll-ms'),
        cfgPushMs:     $('cfg-push-ms'),
        cfgLogMs:      $('cfg-log-ms'),
        cfgCalZmpt1:   $('cfg-cal-zmpt1'),
        cfgCalZmpt2:   $('cfg-cal-zmpt2'),
        cfgCalZmct:    $('cfg-cal-zmct'),
        cfgCalPf:      $('cfg-cal-pf'),
        cfgMaxV:       $('cfg-max-v'),
        cfgMaxA:       $('cfg-max-a'),
        cfgMaxRpm:     $('cfg-max-rpm'),
        cfgMaxT:       $('cfg-max-t'),
        btnSaveCfg:    $('btn-save-cfg'),
        btnRestart:    $('btn-restart'),

        // System info
        sysFw:      $('sys-fw'),
        sysHeap:    $('sys-heap'),
        sysUptime:  $('sys-uptime'),
        sysClients: $('sys-clients'),

        // Toast
        toast:    $('toast'),
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

            const acV  = 30.0 + 25.0 * Math.sin(simStep * 0.5) * (windSpeed / 8.0);
            const acA  = 2.0 + 1.5 * Math.sin(simStep * 0.7) * (windSpeed / 8.0);
            const acP  = Math.max(0, acV * acA * (demoConfigStore.pf || 0.85));
            const acV2 = acV * 0.95 + 1.5 * Math.sin(simStep * 1.1);

            const dcV1 = 12.0 + 2.0 * Math.sin(simStep * 0.4);
            const dcA1 = 3.0 + 2.0 * Math.sin(simStep * 0.6);
            const dcP1 = Math.max(0, dcV1 * dcA1);

            const dcV2 = 24.0 + 3.0 * Math.sin(simStep * 0.35);
            const dcA2 = 1.5 + 1.0 * Math.sin(simStep * 0.55);
            const dcP2 = Math.max(0, dcV2 * dcA2);

            const rpm  = Math.max(0, 800 + 600 * Math.sin(simStep * 0.25) * (windSpeed / 8.0));
            const t1   = 32.0 + 4.0 * Math.sin(simStep * 0.15);
            const t2   = 26.0 + 2.0 * Math.sin(simStep * 0.1);

            updateDashboard({
                acV: acV, acA: Math.max(0, acA), acP: acP, acV2: acV2,
                dcV1: dcV1, dcA1: Math.max(0, dcA1), dcP1: dcP1,
                dcV2: dcV2, dcA2: Math.max(0, dcA2), dcP2: dcP2,
                rpm: rpm, t1: t1, t2: t2,
                uptime: Math.floor(performance.now() / 1000)
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

    // --- Dashboard Update ---
    function updateDashboard(data) {
        const acP  = data.acP != null ? data.acP : 0;
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
        setText(dom.acCur,   fmt(data.acA, 2));
        setText(dom.acVolt2, fmt(data.acV2, 1));
        setText(dom.dcVolt1, fmt(data.dcV1, 2));
        setText(dom.dcCur1,  fmt(data.dcA1, 2));
        setText(dom.dcVolt2, fmt(data.dcV2, 2));
        setText(dom.dcCur2,  fmt(data.dcA2, 2));
        setText(dom.rpm,     data.rpm != null ? Math.round(data.rpm).toString() : '0');
        setText(dom.temp1,   fmt(data.t1, 1));
        setText(dom.temp2,   fmt(data.t2, 1));

        // Progress bars
        setBar(dom.barAcVolt1, data.acV, cfg.maxACVoltage);
        setBar(dom.barAcCur,   data.acA, cfg.maxACCurrent);
        setBar(dom.barAcVolt2, data.acV2, cfg.maxACVoltage);
        setBar(dom.barDcVolt1, data.dcV1, cfg.maxVoltage);
        setBar(dom.barDcCur1,  data.dcA1, cfg.maxCurrent);
        setBar(dom.barDcVolt2, data.dcV2, cfg.maxVoltage);
        setBar(dom.barDcCur2,  data.dcA2, cfg.maxCurrent);
        setBar(dom.barRpm,     data.rpm, cfg.maxRPM);
        setBar(dom.barTemp1,   data.t1, cfg.maxTemp);
        setBar(dom.barTemp2,   data.t2, cfg.maxTemp);

        // Uptime
        if (data.uptime != null) {
            setText(dom.uptimeLabel, 'Uptime: ' + formatUptime(data.uptime));
        }
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
                if (data.apSsid != null)     dom.cfgApSsid.value = data.apSsid;
                if (data.apPass != null)     dom.cfgApPass.value = data.apPass;
                if (data.staEnabled != null) dom.cfgStaEnabled.checked = data.staEnabled;
                if (data.staSsid != null)    dom.cfgStaSsid.value = data.staSsid;
                if (data.staPass != null)    dom.cfgStaPass.value = data.staPass;
                
                if (data.pollMs != null)     dom.cfgPollMs.value = data.pollMs;
                if (data.wsPushMs != null)   dom.cfgPushMs.value = data.wsPushMs;
                if (data.logMs != null)      dom.cfgLogMs.value = data.logMs;

                if (data.zmpt1Cal != null)   dom.cfgCalZmpt1.value = data.zmpt1Cal;
                if (data.zmpt2Cal != null)   dom.cfgCalZmpt2.value = data.zmpt2Cal;
                if (data.zmctCal != null)    dom.cfgCalZmct.value = data.zmctCal;
                if (data.pf != null)         dom.cfgCalPf.value = data.pf;

                if (data.maxV != null)       { dom.cfgMaxV.value = data.maxV; cfg.maxVoltage = data.maxV; }
                if (data.maxA != null)       { dom.cfgMaxA.value = data.maxA; cfg.maxCurrent = data.maxA; }
                if (data.maxRpm != null)     { dom.cfgMaxRpm.value = data.maxRpm; cfg.maxRPM = data.maxRpm; }
                if (data.maxTemp != null)    { dom.cfgMaxT.value = data.maxTemp; cfg.maxTemp = data.maxTemp; }
            })
            .catch(() => showToast('Failed to load settings', 'error'));
    }

    // --- Settings: Load System Info ---
    function loadSysInfo() {
        apiFetch('/api/sysinfo')
            .then(r => r.json())
            .then(data => {
                if (data.fw != null)      dom.sysFw.textContent = data.fw;
                if (data.heap != null)    dom.sysHeap.textContent = (data.heap / 1024).toFixed(1) + ' KB';
                if (data.uptime != null)  dom.sysUptime.textContent = formatUptime(data.uptime);
                if (data.clients != null) dom.sysClients.textContent = data.clients;
            })
            .catch(() => {});
    }

    // --- Settings: Save Config ---
    dom.btnSaveCfg.addEventListener('click', () => {
        const payload = {
            apSsid:     dom.cfgApSsid.value,
            apPass:     dom.cfgApPass.value,
            staEnabled: dom.cfgStaEnabled.checked,
            staSsid:    dom.cfgStaSsid.value,
            staPass:    dom.cfgStaPass.value,
            pollMs:     parseInt(dom.cfgPollMs.value, 10),
            wsPushMs:   parseInt(dom.cfgPushMs.value, 10),
            logMs:      parseInt(dom.cfgLogMs.value, 10),
            zmpt1Cal:   parseFloat(dom.cfgCalZmpt1.value),
            zmpt2Cal:   parseFloat(dom.cfgCalZmpt2.value),
            zmctCal:    parseFloat(dom.cfgCalZmct.value),
            pf:         parseFloat(dom.cfgCalPf.value),
            maxV:       parseFloat(dom.cfgMaxV.value),
            maxA:       parseFloat(dom.cfgMaxA.value),
            maxRpm:     parseInt(dom.cfgMaxRpm.value, 10),
            maxTemp:    parseInt(dom.cfgMaxT.value, 10)
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

    // --- Initialize ---
    connectWS();

})();
