# =============================================================
#  plot_log_data.py — Post-Log Parser, CSV Exporter & Interactive Plotter
#  ESP32 Wind & Solar Monitoring System
#
#  Features:
#    1. Automatically discovers newest log or parses specified .log file
#    2. Extracts full time-series telemetry into structured CSV
#    3. Renders interactive 4-panel visual dashboard (Matplotlib or HTML/Browser)
#    4. Can stream live telemetry directly to SerialPlot or interactive plotter
#
#  Usage:
#    python tools/plot_log_data.py                      # Plots latest log
#    python tools/plot_log_data.py path/to/session.log  # Plots specific log
#    python tools/plot_log_data.py --html               # Generates standalone interactive HTML report
#    python tools/plot_log_data.py --live --port COM3   # Streams live data
# =============================================================

import os
import re
import sys
import glob
import json
import argparse
from datetime import datetime

# Optional Matplotlib import
try:
    import matplotlib.pyplot as plt
    import matplotlib.dates as mdates
    HAS_MATPLOTLIB = True
except ImportError:
    HAS_MATPLOTLIB = False

def find_latest_log():
    """Find the most recent log file in tools/serial_logger/logs/ or current dir."""
    script_dir = os.path.dirname(os.path.abspath(__file__))
    log_dirs = [
        os.path.join(script_dir, "serial_logger", "logs"),
        os.path.join(script_dir, "logs"),
        os.path.join(script_dir, "..", "logs"),
        script_dir
    ]
    all_logs = []
    for d in log_dirs:
        if os.path.exists(d):
            all_logs.extend(glob.glob(os.path.join(d, "*.log")))
            all_logs.extend(glob.glob(os.path.join(d, "*.txt")))
    
    if not all_logs:
        return None
    
    all_logs.sort(key=os.path.getmtime, reverse=True)
    return all_logs[0]

def parse_log_file(log_path):
    """Parse text/ANSI or CSV serial log into time-series data records."""
    if not os.path.exists(log_path):
        print(f"[Error] Log file not found: {log_path}")
        return []

    print(f"[Parser] Reading log file: {log_path} ({os.path.getsize(log_path)} bytes)")

    records = []
    current_rec = {}
    time_idx = 0

    re_ansi = re.compile(r'\x1B(?:[@-Z\\-_]|\[[0-?]*[ -/]*[@-~])')
    
    # Robust regex matchers for production log lines
    re_gen_ac  = re.compile(r'\[Generator AC\]\s+ZMPT1.*?:\s*([\d\.\-]+)\s*V.*?RPM:\s*(\d+)', re.I)
    re_mppt    = re.compile(r'\[MPPT Battery\]\s+INA1:\s*([\d\.\-]+)\s*V\s*\|\s*Charge:\s*([\d\.\-]+)\s*A\s*\(([\d\.\-]+)\s*W\)(?:.*?SoC:\s*([\d\.]+)%)?(?:.*?\(([\d\.]+)\s*Wh\))?', re.I)
    re_inv_dc  = re.compile(r'\[Inverter DC In\]\s+ACS758.*?:\s*([\d\.\-]+)\s*A\s*\|\s*DC Input:\s*([\d\.\-]+)\s*W', re.I)
    re_inv_ac  = re.compile(r'\[Inverter AC Out\]\s*ZMPT2.*?:\s*([\d\.\-]+)\s*V\s*\|\s*ZMCT.*?:\s*([\d\.\-]+)\s*A\s*\|\s*AC Power:\s*([\d\.\-]+)\s*W', re.I)
    re_inv_eff = re.compile(r'\[Inverter Eff\]\s+Efficiency:\s*([\d\.\-]+)\s*%', re.I)
    re_temp    = re.compile(r'\[Temperature\]\s+Gen/Box:\s*([\d\.\-]+).*?/\s*([\d\.\-]+).*?\|\s*ESP32 CPU:\s*([\d\.\-]+)', re.I)
    re_csv_line= re.compile(r'^\s*([\d\.\-]+),([\d\.\-]+),([\d\.\-]+),([\d\.\-]+),([\d\.\-]+),([\d\.\-]+),([\d\.\-]+),([\d\.\-]+),(\d+),([\d\.\-]+),([\d\.\-]+),([\d\.\-]+)\s*$')

    with open(log_path, 'r', encoding='utf-8', errors='replace') as f:
        for line in f:
            clean = re_ansi.sub('', line).strip()
            if not clean:
                continue

            # Check if RAW_PLOT stream line (from hardware test or live raw stream)
            if "RAW_PLOT:" in clean:
                parts = clean.split("RAW_PLOT:", 1)[1].split(",")
                kv = {}
                for p in parts:
                    if "=" in p:
                        k, v = p.split("=", 1)
                        try:
                            kv[k.strip()] = float(v.strip())
                        except ValueError:
                            pass
                rec = {
                    "time_s": time_idx,
                    "gen_ac_v": kv.get("zmpt1_mv", kv.get("ads0", 0.0)) / 1000.0 * 242.0 if kv.get("zmpt1_mv", 0) > 20 else 0.0,
                    "inv_ac_v": kv.get("zmpt2_mv", kv.get("ads1", 0.0)) / 1000.0 * 327.8 if kv.get("zmpt2_mv", 0) > 20 else 0.0,
                    "inv_ac_a": kv.get("zmct_mv", kv.get("ads2", 0.0)) / 1000.0 * 0.22 if kv.get("zmct_mv", 0) > 40 else 0.0,
                    "inv_ac_w": 0.0,
                    "bat_dc_v": kv.get("ina1_v", 0.0),
                    "mppt_dc_a": kv.get("ina1_a", 0.0),
                    "mppt_dc_w": kv.get("ina1_w", 0.0),
                    "bat_soc": max(0.0, min(100.0, (kv.get("ina1_v", 12.0) - 10.5) / (12.8 - 10.5) * 100.0)),
                    "bat_wh": 0.0,
                    "inv_dc_a": kv.get("acs_a", 0.0),
                    "inv_dc_w": kv.get("inv_power", 0.0),
                    "inv_eff": 0.0,
                    "rpm": int(kv.get("rpm", 0)),
                    "temp_gen": kv.get("temp1", 0.0),
                    "temp_box": kv.get("temp2", 0.0),
                    "temp_cpu": kv.get("temp_esp", 0.0)
                }
                rec["inv_ac_w"] = rec["inv_ac_v"] * rec["inv_ac_a"] * 0.85
                records.append(rec)
                time_idx += 1
                continue

            # Check if direct CSV stream line
            m_csv = re_csv_line.match(clean)
            if m_csv:
                try:
                    rec = {
                        "time_s": time_idx * 0.1,  # 10Hz typical for CSV
                        "gen_ac_v": float(m_csv.group(1)),
                        "inv_ac_v": float(m_csv.group(2)),
                        "inv_ac_a": float(m_csv.group(3)),
                        "inv_ac_w": float(m_csv.group(4)),
                        "bat_dc_v": float(m_csv.group(5)),
                        "mppt_dc_a": float(m_csv.group(6)),
                        "mppt_dc_w": float(m_csv.group(5)) * float(m_csv.group(6)),
                        "bat_soc": max(0.0, min(100.0, (float(m_csv.group(5)) - 10.5) / (12.8 - 10.5) * 100.0)),
                        "bat_wh": 0.0,
                        "inv_dc_a": float(m_csv.group(7)),
                        "inv_dc_w": float(m_csv.group(8)),
                        "inv_eff": (float(m_csv.group(4)) / float(m_csv.group(8)) * 100.0) if float(m_csv.group(8)) > 5.0 else 0.0,
                        "rpm": int(m_csv.group(9)),
                        "temp_gen": float(m_csv.group(10)),
                        "temp_box": float(m_csv.group(11)),
                        "temp_cpu": float(m_csv.group(12))
                    }
                    records.append(rec)
                    time_idx += 1
                except ValueError:
                    pass
                continue

            # Parse formatted text blocks
            m = re_gen_ac.search(clean)
            if m:
                current_rec["gen_ac_v"] = float(m.group(1))
                current_rec["rpm"] = int(m.group(2))

            m = re_mppt.search(clean)
            if m:
                current_rec["bat_dc_v"] = float(m.group(1))
                current_rec["mppt_dc_a"] = float(m.group(2))
                current_rec["mppt_dc_w"] = float(m.group(3))
                if m.group(4):
                    current_rec["bat_soc"] = float(m.group(4))
                else:
                    v = current_rec["bat_dc_v"]
                    current_rec["bat_soc"] = max(0.0, min(100.0, (v - 10.5) / (12.8 - 10.5) * 100.0))
                if m.group(5):
                    current_rec["bat_wh"] = float(m.group(5))
                else:
                    current_rec["bat_wh"] = (current_rec["bat_soc"] / 100.0) * (12.0 * 65.0)

            m = re_inv_dc.search(clean)
            if m:
                current_rec["inv_dc_a"] = float(m.group(1))
                current_rec["inv_dc_w"] = float(m.group(2))

            m = re_inv_ac.search(clean)
            if m:
                current_rec["inv_ac_v"] = float(m.group(1))
                current_rec["inv_ac_a"] = float(m.group(2))
                current_rec["inv_ac_w"] = float(m.group(3))

            m = re_inv_eff.search(clean)
            if m:
                current_rec["inv_eff"] = float(m.group(1))

            m = re_temp.search(clean)
            if m:
                current_rec["temp_gen"] = float(m.group(1))
                current_rec["temp_box"] = float(m.group(2))
                current_rec["temp_cpu"] = float(m.group(3))

                # End of a 1Hz frame block
                if "bat_dc_v" in current_rec or "gen_ac_v" in current_rec:
                    current_rec["time_s"] = time_idx
                    if "inv_eff" not in current_rec:
                        p_in = current_rec.get("inv_dc_w", 0.0)
                        p_out = current_rec.get("inv_ac_w", 0.0)
                        current_rec["inv_eff"] = (p_out / p_in * 100.0) if p_in > 5.0 else 0.0
                    records.append(dict(current_rec))
                    time_idx += 1
                current_rec.clear()

    print(f"[Parser] Successfully extracted {len(records)} telemetry data frames.")
    return records

def export_to_csv(records, csv_path):
    """Write records to a CSV file."""
    if not records:
        return
    headers = [
        "Time_s", "Gen_AC_V", "Inv_AC_V", "Inv_AC_A", "Inv_AC_W",
        "Bat_DC_V", "MPPT_DC_A", "MPPT_DC_W", "Bat_SoC_Pct", "Bat_Energy_Wh",
        "Inv_DC_A", "Inv_DC_W", "Inv_Efficiency_Pct", "Rotor_RPM",
        "Temp_Gen_C", "Temp_Box_C", "Temp_ESP_C"
    ]
    with open(csv_path, 'w', encoding='utf-8') as f:
        f.write(",".join(headers) + "\n")
        for r in records:
            row = [
                f"{r.get('time_s', 0):.1f}",
                f"{r.get('gen_ac_v', 0):.1f}",
                f"{r.get('inv_ac_v', 0):.1f}",
                f"{r.get('inv_ac_a', 0):.2f}",
                f"{r.get('inv_ac_w', 0):.1f}",
                f"{r.get('bat_dc_v', 0):.2f}",
                f"{r.get('mppt_dc_a', 0):.2f}",
                f"{r.get('mppt_dc_w', 0):.1f}",
                f"{r.get('bat_soc', 0):.1f}",
                f"{r.get('bat_wh', 0):.1f}",
                f"{r.get('inv_dc_a', 0):.2f}",
                f"{r.get('inv_dc_w', 0):.1f}",
                f"{r.get('inv_eff', 0):.1f}",
                f"{r.get('rpm', 0)}",
                f"{r.get('temp_gen', 0):.1f}",
                f"{r.get('temp_box', 0):.1f}",
                f"{r.get('temp_cpu', 0):.1f}"
            ]
            f.write(",".join(row) + "\n")
    print(f"[Exporter] Saved clean CSV dataset: {csv_path}")

def plot_matplotlib(records, title="ESP32 Wind & Solar System Telemetry"):
    """Plot multi-panel dashboard using Matplotlib."""
    if not HAS_MATPLOTLIB or not records:
        return False

    t = [r.get("time_s", i) for i, r in enumerate(records)]
    gen_v = [r.get("gen_ac_v", 0) for r in records]
    inv_v = [r.get("inv_ac_v", 0) for r in records]
    inv_a = [r.get("inv_ac_a", 0) for r in records]
    inv_w = [r.get("inv_ac_w", 0) for r in records]

    bat_v = [r.get("bat_dc_v", 0) for r in records]
    mppt_a = [r.get("mppt_dc_a", 0) for r in records]
    inv_dc_a = [r.get("inv_dc_a", 0) for r in records]
    inv_dc_w = [r.get("inv_dc_w", 0) for r in records]
    inv_eff = [r.get("inv_eff", 0) for r in records]

    rpm = [r.get("rpm", 0) for r in records]
    t_gen = [r.get("temp_gen", 0) for r in records]
    t_box = [r.get("temp_box", 0) for r in records]
    t_esp = [r.get("temp_cpu", 0) for r in records]

    plt.style.use('dark_background')
    fig, axes = plt.subplots(4, 1, figsize=(14, 10), sharex=True)
    fig.suptitle(title, fontsize=14, fontweight='bold', color='#00e5ff')

    # Subplot 1: AC Voltages & Output Power
    ax1 = axes[0]
    ax1.plot(t, gen_v, color='#00e676', label='Generator AC (V)', linewidth=1.5)
    ax1.plot(t, inv_v, color='#00e5ff', label='Inverter AC Out (V)', linewidth=1.5)
    ax1.set_ylabel('AC Volts (V)', color='#00e5ff')
    ax1.grid(True, alpha=0.25)
    ax1_r = ax1.twinx()
    ax1_r.plot(t, inv_w, color='#ff9800', label='AC Load Power (W)', linestyle='--', linewidth=1.2)
    ax1_r.set_ylabel('Power (W)', color='#ff9800')
    lines1, labels1 = ax1.get_legend_handles_labels()
    lines2, labels2 = ax1_r.get_legend_handles_labels()
    ax1.legend(lines1 + lines2, labels1 + labels2, loc='upper left', framealpha=0.6)

    # Subplot 2: DC Currents & Battery Voltage
    ax2 = axes[1]
    ax2.plot(t, bat_v, color='#2979ff', label='Battery Voltage (V)', linewidth=1.8)
    ax2.set_ylabel('Battery (V)', color='#2979ff')
    ax2.grid(True, alpha=0.25)
    ax2_r = ax2.twinx()
    ax2_r.plot(t, mppt_a, color='#76ff03', label='MPPT Charge Current (A)', linewidth=1.4)
    ax2_r.plot(t, inv_dc_a, color='#f50057', label='Inverter DC Current (A)', linewidth=1.4)
    ax2_r.set_ylabel('DC Current (A)', color='#f50057')
    lines1, labels1 = ax2.get_legend_handles_labels()
    lines2, labels2 = ax2_r.get_legend_handles_labels()
    ax2.legend(lines1 + lines2, labels1 + labels2, loc='upper left', framealpha=0.6)

    # Subplot 3: Power Flow & Efficiency
    ax3 = axes[2]
    ax3.plot(t, inv_dc_w, color='#d500f9', label='Inverter DC In (W)', linewidth=1.5)
    ax3.plot(t, inv_w, color='#ff9800', label='Inverter AC Out (W)', linewidth=1.5)
    ax3.set_ylabel('Power (W)', color='#d500f9')
    ax3.grid(True, alpha=0.25)
    ax3_r = ax3.twinx()
    ax3_r.plot(t, inv_eff, color='#ffeb3b', label='Inverter Efficiency (%)', linestyle=':', linewidth=1.5)
    ax3_r.set_ylabel('Efficiency (%)', color='#ffeb3b')
    lines1, labels1 = ax3.get_legend_handles_labels()
    lines2, labels2 = ax3_r.get_legend_handles_labels()
    ax3.legend(lines1 + lines2, labels1 + labels2, loc='upper left', framealpha=0.6)

    # Subplot 4: Rotor RPM & Temperatures
    ax4 = axes[3]
    ax4.plot(t, rpm, color='#00b0ff', label='Rotor Speed (RPM)', linewidth=1.5)
    ax4.set_ylabel('RPM', color='#00b0ff')
    ax4.set_xlabel('Elapsed Time (seconds)')
    ax4.grid(True, alpha=0.25)
    ax4_r = ax4.twinx()
    ax4_r.plot(t, t_gen, color='#ff5252', label='Generator (°C)', linewidth=1.2)
    ax4_r.plot(t, t_box, color='#ffab00', label='Control Box (°C)', linewidth=1.2)
    ax4_r.plot(t, t_esp, color='#7c4dff', label='ESP32 CPU (°C)', linestyle='--', linewidth=1.2)
    ax4_r.set_ylabel('Temp (°C)', color='#ff5252')
    lines1, labels1 = ax4.get_legend_handles_labels()
    lines2, labels2 = ax4_r.get_legend_handles_labels()
    ax4.legend(lines1 + lines2, labels1 + labels2, loc='upper left', framealpha=0.6)

    plt.tight_layout()
    print("[Plotter] Opening interactive Matplotlib window...")
    plt.show()
    return True

def generate_html_report(records, html_path):
    """Generate a self-contained interactive Chart.js HTML report."""
    if not records:
        return

    labels = [f"{r.get('time_s', i):.1f}s" for i, r in enumerate(records)]
    
    data_gen_v = [r.get("gen_ac_v", 0) for r in records]
    data_inv_v = [r.get("inv_ac_v", 0) for r in records]
    data_inv_a = [r.get("inv_ac_a", 0) for r in records]
    data_inv_w = [r.get("inv_ac_w", 0) for r in records]
    data_bat_v = [r.get("bat_dc_v", 0) for r in records]
    data_mppt_a = [r.get("mppt_dc_a", 0) for r in records]
    data_inv_dc_a = [r.get("inv_dc_a", 0) for r in records]
    data_inv_dc_w = [r.get("inv_dc_w", 0) for r in records]
    data_rpm = [r.get("rpm", 0) for r in records]
    data_temp_gen = [r.get("temp_gen", 0) for r in records]
    data_temp_box = [r.get("temp_box", 0) for r in records]

    html_content = f"""<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32 Wind Monitor — Telemetry Log Analysis</title>
    <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
    <style>
        body {{ background: #0b132b; color: #f8fafc; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; margin: 0; padding: 20px; }}
        h1 {{ text-align: center; color: #00e5ff; font-size: 24px; margin-bottom: 24px; }}
        .grid {{ display: grid; grid-template-columns: 1fr 1fr; gap: 20px; max-width: 1400px; margin: 0 auto; }}
        @media (max-width: 900px) {{ .grid {{ grid-template-columns: 1fr; }} }}
        .card {{ background: #1c2541; border-radius: 12px; padding: 18px; border: 1px solid rgba(255,255,255,0.08); }}
        .card h2 {{ margin-top: 0; font-size: 16px; color: #94a3b8; border-bottom: 1px solid rgba(255,255,255,0.06); padding-bottom: 8px; }}
        canvas {{ width: 100% !important; height: 280px !important; }}
    </style>
</head>
<body>
    <h1>⚡ ESP32 Wind & Solar Monitor — Session Telemetry Analysis</h1>
    <div class="grid">
        <div class="card">
            <h2>AC Voltages & Power Output</h2>
            <canvas id="chartAC"></canvas>
        </div>
        <div class="card">
            <h2>DC Power Flow & Battery</h2>
            <canvas id="chartDC"></canvas>
        </div>
        <div class="card">
            <h2>Inverter Power Balance</h2>
            <canvas id="chartPower"></canvas>
        </div>
        <div class="card">
            <h2>Rotor Speed & Temperatures</h2>
            <canvas id="chartMech"></canvas>
        </div>
    </div>
    <script>
        const labels = {json.dumps(labels)};
        const opts = {{ responsive: true, maintainAspectRatio: false, scales: {{ x: {{ grid: {{ color: 'rgba(255,255,255,0.05)' }} }}, y: {{ grid: {{ color: 'rgba(255,255,255,0.05)' }} }} }} }};
        
        new Chart(document.getElementById('chartAC'), {{
            type: 'line',
            data: {{
                labels: labels,
                datasets: [
                    {{ label: 'Gen AC (V)', data: {json.dumps(data_gen_v)}, borderColor: '#00e676', borderWidth: 2, fill: false }},
                    {{ label: 'Inv AC Out (V)', data: {json.dumps(data_inv_v)}, borderColor: '#00e5ff', borderWidth: 2, fill: false }},
                    {{ label: 'AC Power (W)', data: {json.dumps(data_inv_w)}, borderColor: '#ff9800', borderWidth: 1.5, borderDash: [4,4], fill: false }}
                ]
            }},
            options: opts
        }});

        new Chart(document.getElementById('chartDC'), {{
            type: 'line',
            data: {{
                labels: labels,
                datasets: [
                    {{ label: 'Battery (V)', data: {json.dumps(data_bat_v)}, borderColor: '#2979ff', borderWidth: 2, fill: false }},
                    {{ label: 'MPPT Charge (A)', data: {json.dumps(data_mppt_a)}, borderColor: '#76ff03', borderWidth: 2, fill: false }},
                    {{ label: 'Inv DC Current (A)', data: {json.dumps(data_inv_dc_a)}, borderColor: '#f50057', borderWidth: 2, fill: false }}
                ]
            }},
            options: opts
        }});

        new Chart(document.getElementById('chartPower'), {{
            type: 'line',
            data: {{
                labels: labels,
                datasets: [
                    {{ label: 'Inverter DC In (W)', data: {json.dumps(data_inv_dc_w)}, borderColor: '#d500f9', borderWidth: 2, fill: false }},
                    {{ label: 'Inverter AC Out (W)', data: {json.dumps(data_inv_w)}, borderColor: '#ff9800', borderWidth: 2, fill: false }}
                ]
            }},
            options: opts
        }});

        new Chart(document.getElementById('chartMech'), {{
            type: 'line',
            data: {{
                labels: labels,
                datasets: [
                    {{ label: 'Rotor RPM', data: {json.dumps(data_rpm)}, borderColor: '#00b0ff', borderWidth: 2, fill: false }},
                    {{ label: 'Gen Temp (°C)', data: {json.dumps(data_temp_gen)}, borderColor: '#ff5252', borderWidth: 1.5, fill: false }},
                    {{ label: 'Box Temp (°C)', data: {json.dumps(data_temp_box)}, borderColor: '#ffab00', borderWidth: 1.5, fill: false }}
                ]
            }},
            options: opts
        }});
    </script>
</body>
</html>
"""
    with open(html_path, 'w', encoding='utf-8') as f:
        f.write(html_content)
    print(f"[HTML Report] Saved interactive HTML report: {html_path}")

def main():
    parser = argparse.ArgumentParser(description="ESP32 Wind Monitor — Post-Log Parser, CSV Exporter & Plotter")
    parser.add_argument("logfile", nargs="?", help="Path to .log file (defaults to most recent log in serial_logger/logs)")
    parser.add_argument("--csv", help="Custom path for output CSV file")
    parser.add_argument("--html", action="store_true", help="Generate standalone interactive HTML report")
    parser.add_argument("--no-plot", action="store_true", help="Only parse and export CSV without opening window")
    args = parser.parse_args()

    log_path = args.logfile or find_latest_log()
    if not log_path or not os.path.exists(log_path):
        print("[Error] No valid serial log file found. Run tools/serial_logger.py first to capture data.")
        sys.exit(1)

    records = parse_log_file(log_path)
    if not records:
        print("[Warning] No parseable telemetry data found in log.")
        sys.exit(0)

    base_name = os.path.splitext(log_path)[0]
    csv_path = args.csv or (base_name + "_extracted.csv")
    export_to_csv(records, csv_path)

    html_path = base_name + "_report.html"
    generate_html_report(records, html_path)

    if not args.no_plot:
        if args.html or not HAS_MATPLOTLIB:
            import webbrowser
            print(f"[Plotter] Opening report in default web browser: {html_path}")
            webbrowser.open("file://" + os.path.abspath(html_path))
        else:
            plot_matplotlib(records, f"ESP32 Telemetry: {os.path.basename(log_path)}")

if __name__ == "__main__":
    main()
