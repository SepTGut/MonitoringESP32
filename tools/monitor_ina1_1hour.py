#!/usr/bin/env python3
# =============================================================
#  monitor_ina1_1hour.py — ESP32 MPPT Battery & Power 1-Hour Logger
#
#  Monitors MPPT Battery INA226 #1 (Voltage, Current, Power, Energy, SoC)
#  alongside Inverter DC/AC and Generator telemetry from ESP32 over serial.
#  Displays a real-time terminal dashboard and records continuous
#  high-resolution telemetry to a structured CSV file.
#
#  Usage:
#    python tools/monitor_ina1_1hour.py [--port COM3] [--duration 3600] [--baud 115200]
# =============================================================

import os
import re
import sys
import time
import csv
import signal
import argparse
from datetime import datetime

# Windows UTF-8 console output encoding guard
if hasattr(sys.stdout, 'reconfigure'):
    try:
        sys.stdout.reconfigure(encoding='utf-8', errors='replace')
        sys.stderr.reconfigure(encoding='utf-8', errors='replace')
    except Exception:
        pass

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("[Error] 'pyserial' package is required. Install via: pip install pyserial")
    sys.exit(1)

# ANSI Colors
C_RESET  = "\033[0m"
C_BOLD   = "\033[1m"
C_GREEN  = "\033[32m"
C_YELLOW = "\033[33m"
C_CYAN   = "\033[36m"
C_RED    = "\033[31m"
C_WHITE  = "\033[37m"
C_AMBER  = "\033[38;5;208m"
C_PURPLE = "\033[35m"

# Auto-detect COM port
def find_esp32_port():
    ports = list(serial.tools.list_ports.comports())
    for p in ports:
        desc = (p.description or "").lower()
        hwid = (p.hwid or "").lower()
        if "cp210" in desc or "ch340" in desc or "uart" in desc or "10c4:ea60" in hwid or "1a86:7523" in hwid:
            return p.device
    if ports:
        return ports[0].device
    return "COM3"

def calculate_battery_soc(v):
    if v <= 11.85: return 0.0
    if v >= 12.75: return 100.0
    table = [
        (11.85, 0.0), (11.95, 10.0), (12.05, 25.0), (12.15, 38.0), (12.25, 50.0),
        (12.38, 65.0), (12.50, 75.0), (12.62, 88.0), (12.75, 100.0)
    ]
    for i in range(len(table) - 1):
        if table[i][0] <= v <= table[i+1][0]:
            slope = (table[i+1][1] - table[i][1]) / (table[i+1][0] - table[i][0])
            return table[i][1] + slope * (v - table[i][0])
    return 100.0

def main():
    parser = argparse.ArgumentParser(description="ESP32 MPPT Battery & System Telemetry 1-Hour CSV Logger")
    parser.add_argument("--port", type=str, default="", help="Serial COM Port (default: auto-detect)")
    parser.add_argument("--baud", type=int, default=115200, help="Baud rate (default: 115200)")
    parser.add_argument("--duration", type=int, default=3600, help="Duration in seconds (default: 3600 = 1 hour)")
    parser.add_argument("--outdir", type=str, default="logs", help="Directory for CSV logs")
    args = parser.parse_args()

    port = args.port if args.port else find_esp32_port()
    total_duration = args.duration

    os.makedirs(args.outdir, exist_ok=True)
    session_start_dt = datetime.now()
    timestamp_str = session_start_dt.strftime("%Y%m%d_%H%M%S")
    csv_filename = os.path.join(args.outdir, f"battery_power_log_{timestamp_str}.csv")

    print(f"{C_CYAN}{C_BOLD}================================================================{C_RESET}")
    print(f"{C_CYAN}{C_BOLD}    ESP32 MPPT BATTERY & POWER — REAL-TIME CSV LOGGER           {C_RESET}")
    print(f"{C_CYAN}{C_BOLD}================================================================{C_RESET}")
    print(f"  Port        : {C_YELLOW}{port}{C_RESET} @ {args.baud} baud")
    print(f"  Duration    : {C_YELLOW}{total_duration} seconds{C_RESET} ({total_duration/60:.1f} min / {total_duration/3600:.2f} hr)")
    print(f"  Output CSV  : {C_GREEN}{csv_filename}{C_RESET}")
    print(f"  Battery Ref : {C_WHITE}Lakoni Blue Wolf 12V 65Ah (75D23L, 100% SoH / 780.0 Wh nominal){C_RESET}")
    print(f"  Started at  : {session_start_dt.strftime('%Y-%m-%d %H:%M:%S')}")
    print(f"{C_CYAN}----------------------------------------------------------------{C_RESET}\n")

    try:
        ser = serial.Serial(port, args.baud, timeout=1.0)
    except Exception as e:
        print(f"{C_RED}[Error] Failed to open serial port {port}: {e}{C_RESET}")
        sys.exit(1)

    csv_file = open(csv_filename, "w", newline="", encoding="utf-8")
    csv_writer = csv.writer(csv_file)
    csv_writer.writerow([
        "Timestamp", "Elapsed_Sec", 
        "INA1_Voltage_V", "INA1_Current_A", "INA1_Power_W", "Battery_SoC_Pct", "Battery_Wh_Remaining",
        "Energy_Wh", "Capacity_mAh",
        "ACS758_Inverter_DC_A", "Inverter_DC_Power_W", "Inverter_Efficiency_Pct",
        "Gen_AC_Voltage_V", "Inv_AC_Voltage_V", "Inv_AC_Current_A", "Inv_AC_Power_W",
        "CPU_Temp_C", "RPM"
    ])
    csv_file.flush()

    # Telemetry state
    state = {
        "ina1_v": 0.0, "ina1_a": 0.0, "ina1_w": 0.0,
        "battery_soc": 0.0, "battery_wh": 0.0,
        "acs_a": 0.0, "inv_power": 0.0, "inv_eff": 0.0,
        "gen_ac_v": 0.0, "inv_ac_v": 0.0, "inv_ac_a": 0.0, "inv_ac_w": 0.0,
        "cpu_temp": 0.0, "rpm": 0
    }

    # Statistics
    stats = {
        "samples": 0,
        "v_min": float("inf"), "v_max": float("-inf"), "v_sum": 0.0,
        "a_min": float("inf"), "a_max": float("-inf"), "a_sum": 0.0,
        "p_min": float("inf"), "p_max": float("-inf"), "p_sum": 0.0,
        "energy_wh": 0.0,
        "capacity_mah": 0.0
    }

    start_time = time.time()
    last_energy_calc_time = start_time
    last_print_time = 0
    current_section = ""

    stop_requested = False
    def sig_handler(sig, frame):
        nonlocal stop_requested
        stop_requested = True
    signal.signal(signal.SIGINT, sig_handler)

    try:
        while not stop_requested:
            now = time.time()
            elapsed = now - start_time
            if elapsed >= total_duration:
                break

            line = ser.readline().decode("utf-8", errors="ignore").strip()
            if not line:
                continue

            # 1. Parse Production 1Hz Report
            if "[MPPT Battery]" in line:
                m = re.search(r"INA1:\s*([\d.-]+)\s*V\s*\|\s*Charge:\s*([\d.-]+)\s*A\s*\(([\d.-]+)\s*W\)\s*\|\s*SoC:\s*([\d.-]+)%\s*\(([\d.-]+)\s*Wh\)", line)
                if m:
                    state["ina1_v"] = float(m.group(1))
                    state["ina1_a"] = float(m.group(2))
                    state["ina1_w"] = float(m.group(3))
                    state["battery_soc"] = float(m.group(4))
                    state["battery_wh"] = float(m.group(5))

                    # Accumulate energy & capacity
                    dt_hours = (now - last_energy_calc_time) / 3600.0
                    last_energy_calc_time = now
                    power_now = state["ina1_w"]
                    curr_now = abs(state["ina1_a"])
                    stats["energy_wh"] += power_now * dt_hours
                    stats["capacity_mah"] += (curr_now * 1000.0) * dt_hours

                    stats["samples"] += 1
                    v, a, p = state["ina1_v"], state["ina1_a"], state["ina1_w"]
                    stats["v_sum"] += v; stats["a_sum"] += a; stats["p_sum"] += p
                    if v < stats["v_min"]: stats["v_min"] = v
                    if v > stats["v_max"]: stats["v_max"] = v
                    if a < stats["a_min"]: stats["a_min"] = a
                    if a > stats["a_max"]: stats["a_max"] = a
                    if p < stats["p_min"]: stats["p_min"] = p
                    if p > stats["p_max"]: stats["p_max"] = p

                    # Write row
                    now_str = datetime.now().strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]
                    csv_writer.writerow([
                        now_str, f"{elapsed:.1f}",
                        f"{v:.3f}", f"{a:.3f}", f"{p:.3f}",
                        f"{state['battery_soc']:.1f}", f"{state['battery_wh']:.1f}",
                        f"{stats['energy_wh']:.4f}", f"{stats['capacity_mah']:.2f}",
                        f"{state['acs_a']:.2f}", f"{state['inv_power']:.1f}", f"{state['inv_eff']:.1f}",
                        f"{state['gen_ac_v']:.1f}", f"{state['inv_ac_v']:.1f}", f"{state['inv_ac_a']:.2f}", f"{state['inv_ac_w']:.1f}",
                        f"{state['cpu_temp']:.1f}", state["rpm"]
                    ])
                    csv_file.flush()

            elif "[Inverter DC In]" in line:
                m = re.search(r"ACS758.*?:\s*([\d.-]+)\s*A\s*\|\s*DC Input:\s*([\d.-]+)\s*W", line)
                if m:
                    state["acs_a"] = float(m.group(1))
                    state["inv_power"] = float(m.group(2))

            elif "[Inverter AC Out]" in line:
                m = re.search(r"ZMPT2.*?:\s*([\d.-]+)\s*V\s*\|\s*ZMCT.*?:\s*([\d.-]+)\s*A\s*\|\s*AC Power:\s*([\d.-]+)\s*W", line)
                if m:
                    state["inv_ac_v"] = float(m.group(1))
                    state["inv_ac_a"] = float(m.group(2))
                    state["inv_ac_w"] = float(m.group(3))

            elif "[Inverter Eff]" in line:
                m = re.search(r"Efficiency:\s*([\d.-]+)\s*%", line)
                if m:
                    state["inv_eff"] = float(m.group(1))

            elif "[Generator AC]" in line:
                m = re.search(r"ZMPT1.*?:\s*([\d.-]+)\s*V RMS\s*\|\s*RPM:\s*(\d+)", line)
                if m:
                    state["gen_ac_v"] = float(m.group(1))
                    state["rpm"] = int(m.group(2))

            elif "[Temperature]" in line:
                m = re.search(r"ESP32 CPU:\s*([\d.-]+)°C", line)
                if m:
                    state["cpu_temp"] = float(m.group(1))

            # 2. Parse RAW_PLOT
            elif line.startswith("RAW_PLOT:"):
                pairs = line[9:].split(",")
                for pair in pairs:
                    if "=" in pair:
                        k, v = pair.split("=", 1)
                        k = k.strip()
                        try:
                            num = float(v)
                            if k == "ina1_v": state["ina1_v"] = num
                            elif k == "ina1_a": state["ina1_a"] = num
                            elif k == "ina1_w": state["ina1_w"] = num
                            elif k == "acs_a": state["acs_a"] = num
                            elif k == "rpm": state["rpm"] = int(num)
                            elif k == "temp_esp": state["cpu_temp"] = num
                        except ValueError:
                            pass
                if state["ina1_v"] > 0:
                    state["battery_soc"] = calculate_battery_soc(state["ina1_v"])
                    state["battery_wh"] = (state["battery_soc"] / 100.0) * 780.0

            # 3. Legacy parser fallback
            elif "DC INA226 #1" in line:
                current_section = "INA1"
            elif "DC INA226 #2" in line:
                current_section = "INA2"
            elif line.startswith("AC") or "  AC" in line:
                current_section = "AC"
            elif current_section == "INA1":
                m_v = re.search(r"Voltage:\s*([\d.]+)V", line)
                if m_v: state["ina1_v"] = float(m_v.group(1))
                m_a = re.search(r"Current:\s*([-\d.]+)A", line)
                if m_a: state["ina1_a"] = float(m_a.group(1))
                m_p = re.search(r"Power:\s*([\d.]+)W", line)
                if m_p:
                    state["ina1_w"] = float(m_p.group(1))
                    state["battery_soc"] = calculate_battery_soc(state["ina1_v"])
                    state["battery_wh"] = (state["battery_soc"] / 100.0) * 780.0

            # Console live progress status (~1 sec)
            if now - last_print_time >= 1.0 and stats["samples"] > 0:
                last_print_time = now
                pct = (elapsed / total_duration) * 100.0
                sys.stdout.write(
                    f"\r[{datetime.now().strftime('%H:%M:%S')}] "
                    f"Time: {int(elapsed//60):02d}:{int(elapsed%60):02d} ({pct:4.1f}%) | "
                    f"{C_GREEN}{C_BOLD}V: {state['ina1_v']:5.2f}V{C_RESET} | "
                    f"{C_YELLOW}{C_BOLD}I: {state['ina1_a']:5.2f}A{C_RESET} | "
                    f"{C_CYAN}{C_BOLD}P: {state['ina1_w']:5.2f}W{C_RESET} | "
                    f"{C_WHITE}{C_BOLD}SoC: {state['battery_soc']:4.1f}%{C_RESET} | "
                    f"Energy: {stats['energy_wh']:6.3f} Wh"
                )
                sys.stdout.flush()

    except Exception as e:
        print(f"\n{C_RED}[Error during monitoring]: {e}{C_RESET}")

    finally:
        csv_file.close()
        ser.close()

    total_time = time.time() - start_time
    print(f"\n\n{C_CYAN}{C_BOLD}================================================================{C_RESET}")
    print(f"{C_CYAN}{C_BOLD}                   1-HOUR SESSION COMPLETED                     {C_RESET}")
    print(f"{C_CYAN}{C_BOLD}================================================================{C_RESET}")
    print(f"  Total Duration   : {int(total_time//60)}m {int(total_time%60)}s ({total_time:.1f} sec)")
    print(f"  Total Samples    : {stats['samples']}")
    print(f"  Log File Saved   : {C_GREEN}{csv_filename}{C_RESET}")
    print(f"{C_CYAN}----------------------------------------------------------------{C_RESET}")
    if stats["samples"] > 0:
        v_avg = stats["v_sum"] / stats["samples"]
        a_avg = stats["a_sum"] / stats["samples"]
        p_avg = stats["p_sum"] / stats["samples"]
        print(f"  Voltage (V)      : Min = {stats['v_min']:.2f}V | Max = {stats['v_max']:.2f}V | Avg = {v_avg:.2f}V")
        print(f"  Current (A)      : Min = {stats['a_min']:.2f}A | Max = {stats['a_max']:.2f}A | Avg = {a_avg:.2f}A")
        print(f"  Power (W)        : Min = {stats['p_min']:.2f}W | Max = {stats['p_max']:.2f}W | Avg = {p_avg:.2f}W")
        print(f"  Total Energy     : {C_GREEN}{C_BOLD}{stats['energy_wh']:.4f} Wh{C_RESET} ({stats['capacity_mah']:.2f} mAh)")
    print(f"{C_CYAN}================================================================{C_RESET}\n")

if __name__ == "__main__":
    main()
