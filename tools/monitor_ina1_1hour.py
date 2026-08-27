#!/usr/bin/env python3
# =============================================================
#  monitor_ina1_1hour.py — ESP32 INA226 #1 1-Hour Logger
#
#  Monitors DC INA226 #1 (Voltage, Current, Power, Energy)
#  from ESP32 over serial, displays a live terminal dashboard,
#  and records continuous high-resolution telemetry to a CSV file.
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
    parser = argparse.ArgumentParser(description="ESP32 INA226 #1 1-Hour Monitor & CSV Logger")
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
    csv_filename = os.path.join(args.outdir, f"ina1_log_{timestamp_str}.csv")

    print(f"{C_CYAN}{C_BOLD}================================================================{C_RESET}")
    print(f"{C_CYAN}{C_BOLD}    ESP32 INA226 #1 — REAL-TIME MONITOR & 1-HOUR CSV LOGGER    {C_RESET}")
    print(f"{C_CYAN}{C_BOLD}================================================================{C_RESET}")
    print(f"  Port        : {C_YELLOW}{port}{C_RESET} @ {args.baud} baud")
    print(f"  Duration    : {C_YELLOW}{total_duration} seconds{C_RESET} ({total_duration/60:.1f} minutes / {total_duration/3600:.2f} hours)")
    print(f"  Output CSV  : {C_GREEN}{csv_filename}{C_RESET}")
    print(f"  Battery Ref : {C_WHITE}Lakoni Blue Wolf 12V 65Ah (75D23L, 100% SoH / 65.0Ah / 780.0 Wh nominal){C_RESET}")
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
        "AC_Voltage_V", "AC_Current_A", "AC_Power_W",
        "CPU_Temp_C", "RPM"
    ])
    csv_file.flush()

    # Telemetry state
    state = {
        "ina1_v": 0.0, "ina1_a": 0.0, "ina1_w": 0.0,
        "ac_v": 0.0, "ac_a": 0.0, "ac_w": 0.0,
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

    current_section = ""
    start_time = time.time()
    last_energy_calc_time = start_time
    last_print_time = 0

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

            # Section context tracking
            if "DC INA226 #1" in line:
                current_section = "INA1"
            elif "DC INA226 #2" in line:
                current_section = "INA2"
            elif line.startswith("AC") or "  AC" in line:
                current_section = "AC"
            elif "Temperature:" in line:
                current_section = "OTHER"
                m_temp = re.search(r"Internal CPU:\s*([\d.]+)°C", line)
                if m_temp:
                    state["cpu_temp"] = float(m_temp.group(1))
            elif "RPM:" in line:
                m_rpm = re.search(r"RPM:\s*(\d+)", line)
                if m_rpm:
                    state["rpm"] = int(m_rpm.group(1))

            # Value parsing
            if current_section == "INA1":
                m_v = re.search(r"Voltage:\s*([\d.]+)V", line)
                if m_v: state["ina1_v"] = float(m_v.group(1))
                m_a = re.search(r"Current:\s*([-\d.]+)A", line)
                if m_a: state["ina1_a"] = float(m_a.group(1))
                m_p = re.search(r"Power:\s*([\d.]+)W", line)
                if m_p:
                    state["ina1_w"] = float(m_p.group(1))
                    
                    # New complete INA1 measurement frame received
                    dt_hours = (now - last_energy_calc_time) / 3600.0
                    last_energy_calc_time = now
                    
                    # Accumulate energy & capacity
                    power_now = state["ina1_w"]
                    curr_now = abs(state["ina1_a"])
                    stats["energy_wh"] += power_now * dt_hours
                    stats["capacity_mah"] += (curr_now * 1000.0) * dt_hours

                    stats["samples"] += 1
                    v = state["ina1_v"]
                    a = state["ina1_a"]
                    p = state["ina1_w"]
                    
                    stats["v_sum"] += v
                    stats["a_sum"] += a
                    stats["p_sum"] += p
                    if v < stats["v_min"]: stats["v_min"] = v
                    if v > stats["v_max"]: stats["v_max"] = v
                    if a < stats["a_min"]: stats["a_min"] = a
                    if a > stats["a_max"]: stats["a_max"] = a
                    if p < stats["p_min"]: stats["p_min"] = p
                    if p > stats["p_max"]: stats["p_max"] = p

                    # Calculate Battery SoC for Lakoni 65Ah @ 100% SoH (65.0Ah / 780.0 Wh)
                    soc_pct = calculate_battery_soc(v)
                    wh_rem = (soc_pct / 100.0) * 780.00

                    # Write to CSV
                    now_str = datetime.now().strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]
                    csv_writer.writerow([
                        now_str, f"{elapsed:.1f}",
                        f"{v:.3f}", f"{a:.3f}", f"{p:.3f}",
                        f"{soc_pct:.1f}", f"{wh_rem:.1f}",
                        f"{stats['energy_wh']:.4f}", f"{stats['capacity_mah']:.2f}",
                        f"{state['ac_v']:.1f}", f"{state['ac_a']:.2f}", f"{state['ac_w']:.1f}",
                        f"{state['cpu_temp']:.1f}", state["rpm"]
                    ])
                    csv_file.flush()

            elif current_section == "AC":
                m_ac_v = re.search(r"Voltage:\s*([\d.]+)V", line)
                if m_ac_v: state["ac_v"] = float(m_ac_v.group(1))
                m_ac_a = re.search(r"Current:\s*([-\d.]+)A", line)
                if m_ac_a: state["ac_a"] = float(m_ac_a.group(1))
                m_ac_p = re.search(r"Power:\s*([\d.]+)W", line)
                if m_ac_p: state["ac_w"] = float(m_ac_p.group(1))

            # Display progress update (every ~1 sec)
            if now - last_print_time >= 1.0 and stats["samples"] > 0:
                last_print_time = now
                remaining = max(0, total_duration - elapsed)
                pct = (elapsed / total_duration) * 100.0
                
                v_avg = stats["v_sum"] / stats["samples"]
                a_avg = stats["a_sum"] / stats["samples"]
                p_avg = stats["p_sum"] / stats["samples"]
                cur_soc = calculate_battery_soc(state['ina1_v'])

                sys.stdout.write(
                    f"\r[{datetime.now().strftime('%H:%M:%S')}] "
                    f"Elapsed: {int(elapsed//60):02d}:{int(elapsed%60):02d} ({pct:4.1f}%) | "
                    f"{C_GREEN}{C_BOLD}V: {state['ina1_v']:5.2f}V{C_RESET} | "
                    f"{C_YELLOW}{C_BOLD}I: {state['ina1_a']:5.2f}A{C_RESET} | "
                    f"{C_CYAN}{C_BOLD}P: {state['ina1_w']:5.2f}W{C_RESET} | "
                    f"{C_WHITE}{C_BOLD}SoC: {cur_soc:4.1f}%{C_RESET} | "
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
