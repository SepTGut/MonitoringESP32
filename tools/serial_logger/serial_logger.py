# =============================================================
#  serial_logger.py — ESP32 Dual-Window Serial Dashboard & Waveform Plotter
#
#  Reads ESP32 serial telemetry and automatically opens 2 WINDOWS:
#    - Window 1 (Current Console): Real-Time Diagnostic Dashboard
#    - Window 2 (New Window): Live Raw Sensor Waveform Plotter
#
#  Features:
#    - Seamless parsing for Production 1Hz telemetry, Hardware Test, and RAW_PLOT
#    - Support for ACS758 50A Current Sensor, INA226 x2, ADS1115 16-bit ADC
#    - Battery SoC (%), Energy (Wh), and Inverter Efficiency (%) telemetry
#    - Dual-window live streaming over local TCP socket (127.0.0.1:8888)
#
#  Usage:
#    python tools/serial_logger/serial_logger.py [--port COM3] [--single] [--mode dashboard|plot]
# =============================================================

import os
import re
import sys
import time
import socket
import threading
import argparse
import subprocess
from datetime import datetime

# Enforce UTF-8 encoding on standard output streams to prevent Windows cp1252 crash
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
    print("[Error] 'pyserial' package is not installed.")
    print("Please install it using: pip install pyserial")
    sys.exit(1)

# ANSI Terminal Colors
C_RESET   = "\033[0m"
C_BOLD    = "\033[1m"
C_DIM     = "\033[2m"
C_RED     = "\033[31m"
C_GREEN   = "\033[32m"
C_YELLOW  = "\033[33m"
C_BLUE    = "\033[34m"
C_PURPLE  = "\033[35m"
C_CYAN    = "\033[36m"
C_WHITE   = "\033[37m"
C_AMBER   = "\033[38;5;208m"

# Unified Sensor State Store
telemetry = {
    # Raw ADC & eFuse Millivolts
    "raw_z1": 0, "raw_z2": 0, "raw_zi": 0, "raw_acs": 0,
    "zmpt1_mv": 0.0, "zmpt2_mv": 0.0, "zmct_mv": 0.0, "acs_mv": 0.0,
    
    # External ADS1115 16-Bit ADC (I2C 0x48)
    "ads_connected": False,
    "ads0_mv": 0.0, "ads1_mv": 0.0, "ads2_mv": 0.0, "ads3_mv": 0.0,
    
    # Electrical - Generator AC (ZMPT1)
    "gen_ac_v": 0.0,
    
    # Electrical - MPPT Battery (INA226 #1 @ 0x44)
    "ina1_v": 0.0, "ina1_a": 0.0, "ina1_w": 0.0, "ina1_status": "MISSING",
    "battery_soc": 0.0, "battery_wh": 0.0,
    
    # Electrical - Inverter DC Input (ACS758 on GPIO 33 / ADS A3)
    "acs_a": 0.0, "inv_power": 0.0, "inv_efficiency": 0.0,
    
    # Electrical - Inverter AC Output (ZMPT2 & ZMCT103C)
    "inv_ac_v": 0.0, "inv_ac_a": 0.0, "inv_ac_w": 0.0,
    
    # Electrical - Auxiliary / Control Load (INA226 #2 @ 0x45)
    "ina2_v": 0.0, "ina2_a": 0.0, "ina2_w": 0.0, "ina2_status": "MISSING",
    
    # Mechanical & Environment
    "rpm": 0, "pulses": 0,
    "temp1": 0.0, "temp1_status": "DISCONNECTED",
    "temp2": 0.0, "temp2_status": "DISCONNECTED",
    "cpu_temp": 0.0,
    
    # Diagnostics & System
    "i2c_scan": "Scanning...",
    "last_update": "--:--:--",
    "source_mode": "Auto"
}

# --- Regular Expression Parsers ---
# 1. Production 1Hz Structured Report
re_prod_gen   = re.compile(r"\[Generator AC\]\s*ZMPT1.*?:\s*([\d.-]+)\s*V RMS\s*\|\s*RPM:\s*(\d+)")
re_prod_bat   = re.compile(r"\[MPPT Battery\]\s*INA1:\s*([\d.-]+)\s*V\s*\|\s*Charge:\s*([\d.-]+)\s*A\s*\(([\d.-]+)\s*W\)\s*\|\s*SoC:\s*([\d.-]+)%\s*\(([\d.-]+)\s*Wh\)")
re_prod_invdc = re.compile(r"\[Inverter DC In\]\s*ACS758.*?:\s*([\d.-]+)\s*A\s*\|\s*DC Input:\s*([\d.-]+)\s*W")
re_prod_acsdiag = re.compile(r"\[ACS758 Diag\]\s*GPIO 33:\s*([\d.-]+)\s*mV.*?ADS1115 A3:\s*([\d.-]+)\s*mV")
re_prod_invac = re.compile(r"\[Inverter AC Out\]\s*ZMPT2.*?:\s*([\d.-]+)\s*V\s*\|\s*ZMCT.*?:\s*([\d.-]+)\s*A\s*\|\s*AC Power:\s*([\d.-]+)\s*W")
re_prod_inveff = re.compile(r"\[Inverter Eff\]\s*Efficiency:\s*([\d.-]+)\s*%")
re_prod_ctrl  = re.compile(r"\[Control/Lights\]\s*INA2:\s*([\d.-]+)\s*V\s*\|\s*Load:\s*([\d.-]+)\s*A\s*\(([\d.-]+)\s*W\)")
re_prod_temp  = re.compile(r"\[Temperature\]\s*Gen/Box:\s*([\d.-]+)°C\s*/\s*([\d.-]+)°C\s*\|\s*ESP32 CPU:\s*([\d.-]+)°C")

# 2. Hardware Test Diagnostic Report
re_i2c   = re.compile(r"I2C Bus Scan:\s*(.*)")
re_zmpt1 = re.compile(r"ZMPT101B #1 \(GPIO 34\):.*?(\d+)\s*counts.*?([\d.-]+)\s*mV|ZMPT101B #1 \(GPIO 34\):.*?Raw:\s*(\d+).*?eFuse:\s*([\d.-]+)\s*mV|ZMPT101B #1 \(GPIO 34\):\s*([\d.-]+)\s*mV")
re_zmpt2 = re.compile(r"ZMPT101B #2 \(GPIO 35\):.*?(\d+)\s*counts.*?([\d.-]+)\s*mV|ZMPT101B #2 \(GPIO 35\):.*?Raw:\s*(\d+).*?eFuse:\s*([\d.-]+)\s*mV|ZMPT101B #2 \(GPIO 35\):\s*([\d.-]+)\s*mV")
re_zmct  = re.compile(r"ZMCT103C\s*\(GPIO 32\):.*?(\d+)\s*counts.*?([\d.-]+)\s*mV|ZMCT103C\s*\(GPIO 32\):.*?Raw:\s*(\d+).*?eFuse:\s*([\d.-]+)\s*mV|ZMCT103C\s*\(GPIO 32\):\s*([\d.-]+)\s*mV")
re_acs_hw = re.compile(r"ACS758\s*50A\s*\(GPIO 33\):.*?Raw:\s*(\d+).*?eFuse:\s*([\d.-]+)\s*mV.*?Calc:\s*([\d.-]+)\s*A")

re_ads0  = re.compile(r"Channel A0 \(ZMPT1\)\s*:\s*([\d.-]+)\s*mV")
re_ads1  = re.compile(r"Channel A1 \(ZMPT2\)\s*:\s*([\d.-]+)\s*mV")
re_ads2  = re.compile(r"Channel A2 \(ZMCT\)\s*:\s*([\d.-]+)\s*mV")
re_ads3  = re.compile(r"Channel A3 \(ACS758.*?\)\s*:\s*([\d.-]+)\s*mV.*?Calc:\s*([\d.-]+)\s*A|Channel A3 \(Aux\)\s*:\s*([\d.-]+)\s*mV")

re_ina1  = re.compile(r"INA226 #1 \(0x44\):\s*([\d.-]+)V\s*\|\s*([\d.-]+)A\s*\|\s*([\d.-]+)W\s*\[(\w+)\]")
re_ina2  = re.compile(r"INA226 #2 \(0x45\):\s*([\d.-]+)V\s*\|\s*([\d.-]+)A\s*\|\s*([\d.-]+)W\s*\[(\w+)\]")

re_temp1 = re.compile(r"DS18B20 #1\s*:\s*([\d.-]+)\s*°C\s*\[(\w+)\]")
re_temp2 = re.compile(r"DS18B20 #2\s*:\s*([\d.-]+)\s*°C\s*\[(\w+)\]")
re_cpu   = re.compile(r"ESP32 CPU\s*:\s*([\d.-]+)\s*°C")
re_rpm   = re.compile(r"Total Pulses:\s*(\d+)\s*\|\s*Speed:\s*([\d.-]+)\s*RPM|RPM:\s*(\d+)")

# Local TCP Socket Server State for Multi-Window Streaming
tcp_clients = []
tcp_clients_lock = threading.Lock()

def start_tcp_broadcast_server(port_num=8888):
    """Starts a local TCP socket broadcast server on 127.0.0.1."""
    server_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        server_sock.bind(("127.0.0.1", port_num))
        server_sock.listen(5)
    except Exception as e:
        print(f"{C_YELLOW}[Notice] TCP Broadcast Server bind error on port {port_num}: {e}{C_RESET}")
        return None

    def accept_loop():
        while True:
            try:
                client, _ = server_sock.accept()
                with tcp_clients_lock:
                    tcp_clients.append(client)
            except Exception:
                break

    t = threading.Thread(target=accept_loop, daemon=True)
    t.start()
    return server_sock

def broadcast_line(line):
    """Broadcasts serial line to all connected local plotter windows."""
    with tcp_clients_lock:
        disconnected = []
        data_bytes = line.encode("utf-8")
        for client in tcp_clients:
            try:
                client.sendall(data_bytes)
            except Exception:
                disconnected.append(client)
        for d in disconnected:
            tcp_clients.remove(d)

def spawn_plotter_window(server_port=8888):
    """Spawns Window 2 (Plotter) in a new OS console window."""
    script_path = os.path.abspath(__file__)
    if sys.platform == "win32":
        cmd = f'start "ESP32 Live Waveform Plotter" "{sys.executable}" "{script_path}" --client {server_port}'
        os.system(cmd)
    else:
        try:
            subprocess.Popen(["x-terminal-emulator", "-e", sys.executable, script_path, "--client", str(server_port)])
        except Exception:
            subprocess.Popen([sys.executable, script_path, "--client", str(server_port)])

def auto_detect_port():
    """Auto-detect connected ESP32 COM port."""
    ports = list(serial.tools.list_ports.comports())
    if not ports:
        print(f"{C_RED}[Error] No serial COM ports detected. Connect ESP32 USB cable.{C_RESET}")
        sys.exit(1)
    if len(ports) == 1:
        print(f"{C_GREEN}[Auto-detected] Port: {ports[0].device} ({ports[0].description}){C_RESET}")
        return ports[0].device

    print(f"\n{C_CYAN}Available Serial Ports:{C_RESET}")
    for idx, p in enumerate(ports):
        print(f"  [{idx}] {C_BOLD}{p.device}{C_RESET} - {p.description}")
    
    try:
        choice = input(f"Select port index (0-{len(ports)-1}, default 0): ").strip()
        if not choice:
            return ports[0].device
        idx = int(choice)
        if 0 <= idx < len(ports):
            return ports[idx].device
        return ports[0].device
    except (ValueError, KeyboardInterrupt):
        return ports[0].device

def render_bar(val, max_val, length=24, char="█", color=C_CYAN):
    """Generates an ANSI colorized horizontal bar graph."""
    if max_val <= 0: return ""
    ratio = min(max(val / float(max_val), 0.0), 1.0)
    filled_len = int(round(ratio * length))
    bar = char * filled_len + "░" * (length - filled_len)
    return f"{color}{bar}{C_RESET}"

def clear_screen():
    os.system('cls' if os.name == 'nt' else 'clear')

def parse_serial_line(line):
    """Parse any incoming line (RAW_PLOT, Production Report, or Hardware Test)."""
    global telemetry
    cleaned = line.strip()
    if not cleaned:
        return False

    updated = False

    # 1. Parse Machine-Readable RAW_PLOT Stream
    if cleaned.startswith("RAW_PLOT:"):
        content = cleaned[9:]
        pairs = content.split(",")
        for pair in pairs:
            if "=" in pair:
                k, v = pair.split("=", 1)
                k = k.strip()
                try:
                    if k in ["raw_z1", "raw_z2", "raw_zi", "raw_acs", "pulses"]:
                        telemetry[k] = int(float(v))
                    elif k == "ads0": telemetry["ads0_mv"] = float(v)
                    elif k == "ads1": telemetry["ads1_mv"] = float(v)
                    elif k == "ads2": telemetry["ads2_mv"] = float(v)
                    elif k == "ads3": telemetry["ads3_mv"] = float(v)
                    elif k == "acs_mv": telemetry["acs_mv"] = float(v)
                    elif k == "acs_a": telemetry["acs_a"] = float(v)
                    elif k == "temp_esp": telemetry["cpu_temp"] = float(v)
                    elif k in telemetry: telemetry[k] = float(v)
                except ValueError:
                    pass
        telemetry["last_update"] = datetime.now().strftime("%H:%M:%S")
        telemetry["source_mode"] = "RAW_PLOT Stream"
        return True

    # 2. Parse Production 1Hz Structured Report
    m = re_prod_gen.search(cleaned)
    if m:
        telemetry["gen_ac_v"] = float(m.group(1))
        telemetry["rpm"] = int(m.group(2))
        telemetry["last_update"] = datetime.now().strftime("%H:%M:%S")
        telemetry["source_mode"] = "Production 1Hz Report"
        return True

    m = re_prod_bat.search(cleaned)
    if m:
        telemetry["ina1_v"] = float(m.group(1))
        telemetry["ina1_a"] = float(m.group(2))
        telemetry["ina1_w"] = float(m.group(3))
        telemetry["battery_soc"] = float(m.group(4))
        telemetry["battery_wh"] = float(m.group(5))
        telemetry["ina1_status"] = "OK"
        return True

    m = re_prod_invdc.search(cleaned)
    if m:
        telemetry["acs_a"] = float(m.group(1))
        telemetry["inv_power"] = float(m.group(2))
        return True

    m = re_prod_acsdiag.search(cleaned)
    if m:
        telemetry["acs_mv"] = float(m.group(1))
        telemetry["ads3_mv"] = float(m.group(2))
        return True

    m = re_prod_invac.search(cleaned)
    if m:
        telemetry["inv_ac_v"] = float(m.group(1))
        telemetry["inv_ac_a"] = float(m.group(2))
        telemetry["inv_ac_w"] = float(m.group(3))
        return True

    m = re_prod_inveff.search(cleaned)
    if m:
        telemetry["inv_efficiency"] = float(m.group(1))
        return True

    m = re_prod_ctrl.search(cleaned)
    if m:
        telemetry["ina2_v"] = float(m.group(1))
        telemetry["ina2_a"] = float(m.group(2))
        telemetry["ina2_w"] = float(m.group(3))
        telemetry["ina2_status"] = "OK"
        return True

    m = re_prod_temp.search(cleaned)
    if m:
        telemetry["temp1"] = float(m.group(1))
        telemetry["temp2"] = float(m.group(2))
        telemetry["cpu_temp"] = float(m.group(3))
        telemetry["temp1_status"] = "OK" if telemetry["temp1"] > -50 else "DISCONNECTED"
        telemetry["temp2_status"] = "OK" if telemetry["temp2"] > -50 else "DISCONNECTED"
        return True

    # 3. Parse Hardware Test & Diagnostic Reports
    m = re_i2c.search(cleaned)
    if m:
        telemetry["i2c_scan"] = m.group(1).strip()
        if "0x48" in telemetry["i2c_scan"] or "0x49" in telemetry["i2c_scan"]:
            telemetry["ads_connected"] = True
        return True

    m = re_zmpt1.search(cleaned)
    if m:
        if m.group(1) and m.group(2):
            telemetry["raw_z1"] = int(m.group(1))
            telemetry["zmpt1_mv"] = float(m.group(2))
        elif m.group(3) and m.group(4):
            telemetry["raw_z1"] = int(m.group(3))
            telemetry["zmpt1_mv"] = float(m.group(4))
        elif m.group(5):
            telemetry["zmpt1_mv"] = float(m.group(5))
        return True

    m = re_zmpt2.search(cleaned)
    if m:
        if m.group(1) and m.group(2):
            telemetry["raw_z2"] = int(m.group(1))
            telemetry["zmpt2_mv"] = float(m.group(2))
        elif m.group(3) and m.group(4):
            telemetry["raw_z2"] = int(m.group(3))
            telemetry["zmpt2_mv"] = float(m.group(4))
        elif m.group(5):
            telemetry["zmpt2_mv"] = float(m.group(5))
        return True

    m = re_zmct.search(cleaned)
    if m:
        if m.group(1) and m.group(2):
            telemetry["raw_zi"] = int(m.group(1))
            telemetry["zmct_mv"] = float(m.group(2))
        elif m.group(3) and m.group(4):
            telemetry["raw_zi"] = int(m.group(3))
            telemetry["zmct_mv"] = float(m.group(4))
        elif m.group(5):
            telemetry["zmct_mv"] = float(m.group(5))
        return True

    m = re_acs_hw.search(cleaned)
    if m:
        telemetry["raw_acs"] = int(m.group(1))
        telemetry["acs_mv"] = float(m.group(2))
        telemetry["acs_a"] = float(m.group(3))
        return True

    m = re_ads0.search(cleaned); 
    if m: telemetry["ads0_mv"] = float(m.group(1)); telemetry["ads_connected"] = True; return True
    m = re_ads1.search(cleaned); 
    if m: telemetry["ads1_mv"] = float(m.group(1)); telemetry["ads_connected"] = True; return True
    m = re_ads2.search(cleaned); 
    if m: telemetry["ads2_mv"] = float(m.group(1)); telemetry["ads_connected"] = True; return True
    m = re_ads3.search(cleaned); 
    if m: 
        if m.group(1):
            telemetry["ads3_mv"] = float(m.group(1))
            if m.group(2): telemetry["acs_a"] = float(m.group(2))
        elif m.group(3):
            telemetry["ads3_mv"] = float(m.group(3))
        telemetry["ads_connected"] = True
        return True

    m = re_ina1.search(cleaned)
    if m:
        telemetry["ina1_v"] = float(m.group(1))
        telemetry["ina1_a"] = float(m.group(2))
        telemetry["ina1_w"] = float(m.group(3))
        telemetry["ina1_status"] = m.group(4)
        return True

    m = re_ina2.search(cleaned)
    if m:
        telemetry["ina2_v"] = float(m.group(1))
        telemetry["ina2_a"] = float(m.group(2))
        telemetry["ina2_w"] = float(m.group(3))
        telemetry["ina2_status"] = m.group(4)
        return True

    m = re_temp1.search(cleaned)
    if m: telemetry["temp1"] = float(m.group(1)); telemetry["temp1_status"] = m.group(2); return True

    m = re_temp2.search(cleaned)
    if m: telemetry["temp2"] = float(m.group(1)); telemetry["temp2_status"] = m.group(2); return True

    m = re_cpu.search(cleaned)
    if m: telemetry["cpu_temp"] = float(m.group(1)); return True

    m = re_rpm.search(cleaned)
    if m:
        if m.group(1) and m.group(2):
            telemetry["pulses"] = int(m.group(1))
            telemetry["rpm"] = float(m.group(2))
        elif m.group(3):
            telemetry["rpm"] = float(m.group(3))
        return True

    return False

def render_plotter(port, log_filename):
    """Render real-time raw sensor plotter dashboard (WINDOW 2)."""
    clear_screen()
    t = telemetry

    print(f"{C_BOLD}{C_CYAN}======================================================================{C_RESET}")
    print(f"{C_BOLD}{C_CYAN}  [WINDOW 2] ESP32 LIVE SENSOR RAW WAVEFORM PLOTTER                   {C_RESET}")
    print(f"{C_CYAN}  Port/Stream: {C_BOLD}{port}{C_RESET}{C_CYAN} | Mode: {t['source_mode']} | Update: {t['last_update']}{C_RESET}")
    print(f"{C_BOLD}{C_CYAN}======================================================================{C_RESET}\n")

    print(f"  {C_BOLD}{C_YELLOW}[1. INTERNAL ESP32 ADC RAW COUNTS (0 - 4095)]{C_RESET}")
    b_z1 = render_bar(t["raw_z1"], 4095, 24, "█", C_GREEN)
    b_z2 = render_bar(t["raw_z2"], 4095, 24, "█", C_GREEN)
    b_zi = render_bar(t["raw_zi"], 4095, 24, "█", C_YELLOW)
    b_acs = render_bar(t["raw_acs"], 4095, 24, "█", C_PURPLE)
    print(f"    GPIO 34 (ZMPT1)  : {int(t['raw_z1']):4d} counts {b_z1}")
    print(f"    GPIO 35 (ZMPT2)  : {int(t['raw_z2']):4d} counts {b_z2}")
    print(f"    GPIO 32 (ZMCT)   : {int(t['raw_zi']):4d} counts {b_zi}")
    print(f"    GPIO 33 (ACS758) : {int(t['raw_acs']):4d} counts {b_acs}\n")

    print(f"  {C_BOLD}{C_GREEN}[2. eFUSE CALIBRATED SENSOR MILLIVOLTS (0 - 3300 mV)]{C_RESET}")
    b_mv1 = render_bar(t["zmpt1_mv"], 3300, 24, "█", C_CYAN)
    b_mv2 = render_bar(t["zmpt2_mv"], 3300, 24, "█", C_CYAN)
    b_mvi = render_bar(t["zmct_mv"], 3300, 24, "█", C_AMBER)
    b_macs = render_bar(t["acs_mv"], 3300, 24, "█", C_RED)
    print(f"    ZMPT101B #1 Gen  : {t['zmpt1_mv']:6.1f} mV {b_mv1}")
    print(f"    ZMPT101B #2 Inv  : {t['zmpt2_mv']:6.1f} mV {b_mv2}")
    print(f"    ZMCT103C AC Curr : {t['zmct_mv']:6.1f} mV {b_mvi}")
    print(f"    ACS758 Inverter  : {t['acs_mv']:6.1f} mV {b_macs}\n")

    print(f"  {C_BOLD}{C_PURPLE}[3. EXTERNAL ADS1115 16-BIT ADC (0 - 4096 mV)]{C_RESET}")
    b_a0 = render_bar(t["ads0_mv"], 4096, 24, "█", C_PURPLE)
    b_a1 = render_bar(t["ads1_mv"], 4096, 24, "█", C_PURPLE)
    b_a2 = render_bar(t["ads2_mv"], 4096, 24, "█", C_AMBER)
    b_a3 = render_bar(t["ads3_mv"], 4096, 24, "█", C_RED)
    print(f"    A0 (ZMPT1 Gen)   : {t['ads0_mv']:8.3f} mV {b_a0}")
    print(f"    A1 (ZMPT2 Inv)   : {t['ads1_mv']:8.3f} mV {b_a1}")
    print(f"    A2 (ZMCT AC Curr): {t['ads2_mv']:8.3f} mV {b_a2}")
    print(f"    A3 (ACS758 Curr) : {t['ads3_mv']:8.3f} mV {b_a3}\n")

    print(f"  {C_BOLD}{C_BLUE}[4. POWER FLOW & ROTOR SPEED]{C_RESET}")
    b_ina1 = render_bar(t["ina1_w"], 120, 18, "█", C_BLUE)
    b_invp = render_bar(t["inv_power"] if t["inv_power"] > 0 else t["inv_ac_w"], 120, 18, "█", C_AMBER)
    b_rpm  = render_bar(t["rpm"], 3000, 18, "█", C_GREEN)
    print(f"    MPPT Battery INA1: {t['ina1_v']:5.2f} V | {t['ina1_a']:5.2f} A | {t['ina1_w']:6.2f} W {b_ina1}")
    print(f"    Inverter DC/AC   : {t['acs_a']:5.2f} A DC | AC: {t['inv_ac_w']:6.2f} W (Eff: {t['inv_efficiency']:4.1f}%) {b_invp}")
    print(f"    Rotor Speed      : {t['rpm']:5.0f} RPM {b_rpm}")
    print(f"    ESP32 CPU Temp   : {t['cpu_temp']:5.1f} °C\n")

    print(f"{C_BOLD}{C_CYAN}======================================================================{C_RESET}")
    print(f"  Press {C_BOLD}Ctrl+C{C_RESET} to close plotter window.")

def render_dashboard(port, log_filename):
    """Render real-time diagnostic table dashboard (WINDOW 1)."""
    clear_screen()
    t = telemetry

    print(f"{C_BOLD}{C_CYAN}======================================================================{C_RESET}")
    print(f"{C_BOLD}{C_CYAN}  [WINDOW 1] ESP32 WIND & SOLAR MONITOR REAL-TIME DASHBOARD            {C_RESET}")
    print(f"{C_CYAN}  Port: {C_BOLD}{port}{C_RESET}{C_CYAN} | Mode: {t['source_mode']} | Log: {os.path.basename(log_filename)}{C_RESET}")
    print(f"{C_CYAN}  [TCP Broadcast Server: Active on 127.0.0.1:8888 -> Window 2 Plotter]{C_RESET}")
    print(f"{C_BOLD}{C_CYAN}======================================================================{C_RESET}\n")

    print(f"  {C_BOLD}{C_PURPLE}[I2C BUS DISCOVERY SCAN]{C_RESET}")
    print(f"    Detected Devices : {C_GREEN}{t['i2c_scan']}{C_RESET}\n")

    print(f"  {C_BOLD}{C_CYAN}[1. WIND GENERATOR AC & MECHANICAL SPEED]{C_RESET}")
    print(f"    Generator AC (ZMPT1 A0) : {C_GREEN}{t['gen_ac_v']:5.1f} V RMS{C_RESET} | eFuse: {t['zmpt1_mv']:6.1f} mV (Raw: {t['raw_z1']})")
    print(f"    Rotor Speed & Pulses    : {C_GREEN}{t['rpm']:5.0f} RPM{C_RESET} ({t['pulses']} Pulses)\n")

    print(f"  {C_BOLD}{C_BLUE}[2. MPPT SOLAR/WIND BATTERY (INA226 #1 @ 0x44)]{C_RESET}")
    ina1_st = f"{C_GREEN}[OK]{C_RESET}" if t["ina1_status"] == "OK" else f"{C_RED}[MISSING]{C_RESET}"
    print(f"    Battery Voltage  : {C_GREEN}{t['ina1_v']:5.2f} V{C_RESET} | Charge Current: {C_GREEN}{t['ina1_a']:5.2f} A{C_RESET} | Power: {C_CYAN}{t['ina1_w']:6.2f} W{C_RESET} {ina1_st}")
    print(f"    State of Charge  : {C_WHITE}{C_BOLD}{t['battery_soc']:5.1f} %{C_RESET} | Energy Remaining: {C_AMBER}{t['battery_wh']:6.1f} Wh{C_RESET} (Nominal 780Wh)\n")

    print(f"  {C_BOLD}{C_AMBER}[3. INVERTER CONVERSION SYSTEM]{C_RESET}")
    print(f"    DC Input (ACS758 GPIO 33): {C_GREEN}{t['acs_a']:5.2f} A{C_RESET} | Input Power: {C_CYAN}{t['inv_power']:6.1f} W{C_RESET} | Bias: {t['acs_mv']:6.1f} mV")
    print(f"    AC Output (ZMPT2 & ZMCT) : {C_GREEN}{t['inv_ac_v']:5.1f} V{C_RESET} | Load Current: {C_GREEN}{t['inv_ac_a']:5.2f} A{C_RESET} | AC Power: {C_CYAN}{t['inv_ac_w']:6.1f} W{C_RESET}")
    if t["inv_efficiency"] > 0:
        print(f"    Conversion Efficiency    : {C_GREEN}{C_BOLD}{t['inv_efficiency']:5.1f} %{C_RESET}\n")
    else:
        print(f"    Conversion Efficiency    : {C_DIM}--.- % (Standby / Low Load){C_RESET}\n")

    print(f"  {C_BOLD}{C_PURPLE}[4. AUXILIARY / CONTROL LOAD (INA226 #2 @ 0x45)]{C_RESET}")
    ina2_st = f"{C_GREEN}[OK]{C_RESET}" if t["ina2_status"] == "OK" else f"{C_RED}[MISSING]{C_RESET}"
    print(f"    Aux Load (Lights/Ctrl)   : {C_GREEN}{t['ina2_v']:5.2f} V{C_RESET} | Current: {C_GREEN}{t['ina2_a']:5.2f} A{C_RESET} | Power: {C_CYAN}{t['ina2_w']:6.2f} W{C_RESET} {ina2_st}\n")

    print(f"  {C_BOLD}{C_YELLOW}[5. ANALOG FRONT-END & ADS1115 16-BIT ADC (0x48)]{C_RESET}")
    if t["ads_connected"] or t["ads0_mv"] > 0:
        print(f"    Channel A0 (ZMPT1 Gen) : {C_PURPLE}{t['ads0_mv']:8.3f} mV{C_RESET}   Channel A1 (ZMPT2 Inv) : {C_PURPLE}{t['ads1_mv']:8.3f} mV{C_RESET}")
        print(f"    Channel A2 (ZMCT Curr) : {C_AMBER}{t['ads2_mv']:8.3f} mV{C_RESET}   Channel A3 (ACS758 In) : {C_RED}{t['ads3_mv']:8.3f} mV{C_RESET}\n")
    else:
        print(f"    Status: {C_YELLOW}Using internal ESP32 eFuse ADC (ZMPT1:{t['zmpt1_mv']:.0f}mV, ZMPT2:{t['zmpt2_mv']:.0f}mV, ZMCT:{t['zmct_mv']:.0f}mV){C_RESET}\n")

    print(f"  {C_BOLD}{C_GREEN}[6. TEMPERATURE MONITORING]{C_RESET}")
    t1_st = f"{C_GREEN}[OK]{C_RESET}" if t["temp1_status"] == "OK" else f"{C_RED}[DISCONNECTED]{C_RESET}"
    t2_st = f"{C_GREEN}[OK]{C_RESET}" if t["temp2_status"] == "OK" else f"{C_RED}[DISCONNECTED]{C_RESET}"
    print(f"    Generator Box Temp : {C_GREEN}{t['temp1']:5.1f} °C{C_RESET} {t1_st}   Enclosure Temp : {C_GREEN}{t['temp2']:5.1f} °C{C_RESET} {t2_st}")
    print(f"    ESP32 Internal CPU : {C_YELLOW}{t['cpu_temp']:5.1f} °C{C_RESET}\n")

    print(f"{C_BOLD}{C_CYAN}======================================================================{C_RESET}")
    print(f"  Commands: Type {C_BOLD}CALIBRATE{C_RESET}, {C_BOLD}I2C SCAN{C_RESET}, or {C_BOLD}REBOOT{C_RESET} | Press {C_BOLD}Ctrl+C{C_RESET} to exit.")

def run_tcp_client(server_port=8888):
    """Execution loop for Window 2 (Live Plotter Client Window)."""
    client_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        client_sock.connect(("127.0.0.1", server_port))
    except Exception as e:
        print(f"{C_RED}[Error] Could not connect Window 2 Plotter to local stream on port {server_port}: {e}{C_RESET}")
        time.sleep(3)
        return

    buffer = ""
    render_plotter(f"TCP Local Stream ({server_port})", "plotter_stream.log")

    try:
        while True:
            data = client_sock.recv(4096).decode("utf-8", errors="replace")
            if not data:
                break
            buffer += data
            while "\n" in buffer:
                line, buffer = buffer.split("\n", 1)
                if parse_serial_line(line):
                    render_plotter(f"TCP Local Stream ({server_port})", "plotter_stream.log")
    except (socket.error, KeyboardInterrupt):
        pass
    finally:
        client_sock.close()
        print(f"\n{C_CYAN}Window 2 Plotter closed.{C_RESET}")

def run_logger(port=None, baud_rate=115200, mode="dual"):
    """Main serial reading & rendering loop (Window 1 Master)."""
    if mode == "client":
        return

    if not port:
        port = auto_detect_port()

    script_dir = os.path.dirname(os.path.abspath(__file__))
    log_dir = os.path.join(script_dir, "logs")
    os.makedirs(log_dir, exist_ok=True)

    timestamp_str = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    log_filename = os.path.join(log_dir, f"serial_logger_{timestamp_str}.log")

    print(f"{C_GREEN}Opening serial connection on {port} at {baud_rate} baud...{C_RESET}")

    server_port = 8888
    server_sock = start_tcp_broadcast_server(server_port)

    # If dual-window mode, spawn Window 2 (Live Waveform Plotter)
    if mode == "dual":
        print(f"{C_CYAN}Spawning Window 2 (Live Waveform Plotter)...{C_RESET}")
        time.sleep(0.5)
        spawn_plotter_window(server_port)

    try:
        ser = serial.Serial(port, baud_rate, timeout=1)
        ser.dtr = False; ser.rts = False; time.sleep(0.1)
        ser.dtr = True; ser.rts = True

        log_file = open(log_filename, "a", encoding="utf-8")
        log_file.write(f"=== Unified Serial Logger Session Started at {datetime.now()} ===\n")
        log_file.flush()

        if mode == "plot":
            render_plotter(port, log_filename)
        else:
            render_dashboard(port, log_filename)

        while True:
            if ser.in_waiting > 0:
                raw_bytes = ser.readline()
                line = raw_bytes.decode("utf-8", errors="replace")
                
                log_file.write(line)
                log_file.flush()

                # Broadcast line to Window 2 (Plotter Window)
                broadcast_line(line)

                updated = parse_serial_line(line)

                if updated or "└──────────" in line or "=============" in line:
                    if mode == "plot":
                        render_plotter(port, log_filename)
                    else:
                        render_dashboard(port, log_filename)
            else:
                time.sleep(0.02)

    except serial.SerialException as e:
        print(f"{C_RED}\nSerial Error on {port}: {e}{C_RESET}")
    except KeyboardInterrupt:
        print(f"{C_CYAN}\nDisconnected. Session log saved: {log_filename}{C_RESET}")
    finally:
        if 'ser' in locals() and ser.is_open:
            ser.close()
        if 'log_file' in locals() and not log_file.closed:
            log_file.close()
        if server_sock:
            server_sock.close()

def main():
    parser = argparse.ArgumentParser(description="ESP32 Dual-Window Serial Dashboard & Waveform Plotter")
    parser.add_argument("--port", help="ESP32 serial COM port (e.g. COM3 or /dev/ttyUSB0)")
    parser.add_argument("--baud", type=int, default=115200, help="Serial baud rate (default: 115200)")
    parser.add_argument("--mode", choices=["dual", "dashboard", "plot"], default="dual", help="View mode")
    parser.add_argument("--plot", action="store_true", help="Shortcut for single plot window")
    parser.add_argument("--single", action="store_true", help="Launch single dashboard window without opening 2nd plotter window")
    parser.add_argument("--client", type=int, help="Run in TCP client mode (internal Window 2 process)")
    args = parser.parse_args()

    try:
        if args.client:
            run_tcp_client(args.client)
        else:
            if args.plot:
                mode = "plot"
            elif args.single:
                mode = "dashboard"
            else:
                mode = args.mode
            run_logger(args.port, args.baud, mode)
    except Exception as e:
        print(f"\n{C_RED}[Notice] Serial logger stopped: {e}{C_RESET}")
        try:
            input("\nPress Enter to exit...")
        except (KeyboardInterrupt, EOFError):
            pass

if __name__ == "__main__":
    main()
