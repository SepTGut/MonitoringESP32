# =============================================================
#  serial_logger.py — ESP32 Dual-Window Serial Dashboard & Waveform Plotter
#
#  Reads ESP32 serial telemetry and automatically opens 2 WINDOWS:
#    - Window 1 (Current Console): ESP32 Real-Time Serial Dashboard
#    - Window 2 (New Window): Live Raw Sensor Waveform Plotter
#
#  Solves Windows COM port exclusive locking by broadcasting telemetry
#  over a local TCP socket (127.0.0.1:8888) to Window 2.
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
C_RED     = "\033[31m"
C_GREEN   = "\033[32m"
C_YELLOW  = "\033[33m"
C_BLUE    = "\033[34m"
C_PURPLE  = "\033[35m"
C_CYAN    = "\033[36m"
C_AMBER   = "\033[38;5;208m"

# Unified Sensor State Store
telemetry = {
    # Raw ADC & eFuse / ADS1115 Values
    "raw_z1": 0, "raw_z2": 0, "raw_zi": 0,
    "zmpt1_mv": 0.0, "zmpt2_mv": 0.0, "zmct_mv": 0.0,
    "ads_connected": False,
    "ads0_mv": 0.0, "ads1_mv": 0.0, "ads2_mv": 0.0, "ads3_mv": 0.0,
    
    # Processed Electrical & Power
    "ac_voltage": 0.0, "ac_voltage2": 0.0, "ac_current": 0.0, "ac_power": 0.0,
    "ina1_v": 0.0, "ina1_a": 0.0, "ina1_w": 0.0, "ina1_status": "MISSING",
    "ina2_v": 0.0, "ina2_a": 0.0, "ina2_w": 0.0, "ina2_status": "MISSING",
    
    # MAX9814 Sound Microphone
    "raw_mic": 0, "mic_mv": 0.0, "mic_vpp": 0.0,
    
    # Mechanical & Environment
    "rpm": 0, "pulses": 0,
    "temp1": 0.0, "temp1_status": "DISCONNECTED",
    "temp2": 0.0, "temp2_status": "DISCONNECTED",
    "cpu_temp": 0.0,
    
    # Diagnostics & System
    "i2c_scan": "Scanning...",
    "last_update": "--:--:--"
}

# Regex Parsers
re_i2c   = re.compile(r"I2C Bus Scan:\s*(.*)")
re_zmpt1 = re.compile(r"ZMPT101B #1 \(GPIO 34\):.*?(\d+)\s*counts.*?([\d.-]+)\s*mV|ZMPT101B #1 \(GPIO 34\):\s*([\d.-]+)\s*mV")
re_zmpt2 = re.compile(r"ZMPT101B #2 \(GPIO 35\):.*?(\d+)\s*counts.*?([\d.-]+)\s*mV|ZMPT101B #2 \(GPIO 35\):\s*([\d.-]+)\s*mV")
re_zmct  = re.compile(r"ZMCT103C\s*\(GPIO 32\):.*?(\d+)\s*counts.*?([\d.-]+)\s*mV|ZMCT103C\s*\(GPIO 32\):\s*([\d.-]+)\s*mV")

re_ads0  = re.compile(r"Channel A0 \(ZMPT1\):\s*([\d.-]+)\s*mV")
re_ads1  = re.compile(r"Channel A1 \(ZMPT2\):\s*([\d.-]+)\s*mV")
re_ads2  = re.compile(r"Channel A2 \(ZMCT\)\s*:\s*([\d.-]+)\s*mV")
re_ads3  = re.compile(r"Channel A3 \(Aux\)\s*:\s*([\d.-]+)\s*mV")

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
        print(f"{C_GREEN}[Auto-detected] Port: {ports[0].device}{C_RESET}")
        return ports[0].device

    print(f"\n{C_CYAN}Available Serial Ports:{C_RESET}")
    for idx, p in enumerate(ports):
        print(f"  [{idx}] {p.device} - {p.description}")
    
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

def render_bar(val, max_val, length=25, char="█", color=C_CYAN):
    """Generates an ANSI colorized horizontal bar graph."""
    if max_val <= 0: return ""
    ratio = min(max(val / float(max_val), 0.0), 1.0)
    filled_len = int(round(ratio * length))
    bar = char * filled_len + "░" * (length - filled_len)
    return f"{color}{bar}{C_RESET}"

def clear_screen():
    os.system('cls' if os.name == 'nt' else 'clear')

def parse_serial_line(line):
    """Parse any incoming line (RAW_PLOT, Hardware Test, or Main Production log)."""
    global telemetry
    cleaned = line.strip()

    # 1. Parse Machine-Readable RAW_PLOT Stream
    if cleaned.startswith("RAW_PLOT:"):
        content = cleaned[9:]
        pairs = content.split(",")
        for pair in pairs:
            if "=" in pair:
                k, v = pair.split("=", 1)
                k = k.strip()
                try:
                    if k in ["raw_z1", "raw_z2", "raw_zi", "pulses"]:
                        telemetry[k] = int(float(v))
                    elif k == "ads0": telemetry["ads0_mv"] = float(v)
                    elif k == "ads1": telemetry["ads1_mv"] = float(v)
                    elif k == "ads2": telemetry["ads2_mv"] = float(v)
                    elif k == "ads3": telemetry["ads3_mv"] = float(v)
                    elif k == "temp_esp": telemetry["cpu_temp"] = float(v)
                    elif k in telemetry: telemetry[k] = float(v)
                except ValueError:
                    pass
        telemetry["last_update"] = datetime.now().strftime("%H:%M:%S")
        return True

    # 2. Parse Hardware Test & Production Reports
    m = re_i2c.search(cleaned)
    if m:
        telemetry["i2c_scan"] = m.group(1).strip()
        if "0x48" in telemetry["i2c_scan"] or "0x49" in telemetry["i2c_scan"]:
            telemetry["ads_connected"] = True
        return True

    m = re_zmpt1.search(cleaned)
    if m:
        if m.group(1): telemetry["raw_z1"] = int(m.group(1))
        if m.group(2): telemetry["zmpt1_mv"] = float(m.group(2))
        elif m.group(3): telemetry["zmpt1_mv"] = float(m.group(3))
        return True

    m = re_zmpt2.search(cleaned)
    if m:
        if m.group(1): telemetry["raw_z2"] = int(m.group(1))
        if m.group(2): telemetry["zmpt2_mv"] = float(m.group(2))
        elif m.group(3): telemetry["zmpt2_mv"] = float(m.group(3))
        return True

    m = re_zmct.search(cleaned)
    if m:
        if m.group(1): telemetry["raw_zi"] = int(m.group(1))
        if m.group(2): telemetry["zmct_mv"] = float(m.group(2))
        elif m.group(3): telemetry["zmct_mv"] = float(m.group(3))
        return True

    m = re_ads0.search(cleaned); 
    if m: telemetry["ads0_mv"] = float(m.group(1)); return True
    m = re_ads1.search(cleaned); 
    if m: telemetry["ads1_mv"] = float(m.group(1)); return True
    m = re_ads2.search(cleaned); 
    if m: telemetry["ads2_mv"] = float(m.group(1)); return True
    m = re_ads3.search(cleaned); 
    if m: telemetry["ads3_mv"] = float(m.group(1)); return True

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
    print(f"{C_CYAN}  Port/Stream: {C_BOLD}{port}{C_RESET}{C_CYAN} | Last Update: {t['last_update']}{C_RESET}")
    print(f"{C_BOLD}{C_CYAN}======================================================================{C_RESET}\n")

    print(f"  {C_BOLD}{C_YELLOW}[1. INTERNAL ESP32 ADC RAW COUNTS (0 - 4095)]{C_RESET}")
    b_z1 = render_bar(t["raw_z1"], 4095, 30, "█", C_GREEN)
    b_z2 = render_bar(t["raw_z2"], 4095, 30, "█", C_GREEN)
    b_zi = render_bar(t["raw_zi"], 4095, 30, "█", C_YELLOW)
    b_mic = render_bar(t["raw_mic"], 4095, 30, "█", C_PURPLE)
    print(f"    GPIO 34 (ZMPT1) : {int(t['raw_z1']):4d} counts {b_z1}")
    print(f"    GPIO 35 (ZMPT2) : {int(t['raw_z2']):4d} counts {b_z2}")
    print(f"    GPIO 32 (ZMCT)  : {int(t['raw_zi']):4d} counts {b_zi}")
    print(f"    GPIO 33 (MAX98) : {int(t['raw_mic']):4d} counts {b_mic}\n")

    print(f"  {C_BOLD}{C_GREEN}[2. eFUSE CALIBRATED SENSOR MILLIVOLTS (0 - 3300 mV)]{C_RESET}")
    b_mv1 = render_bar(t["zmpt1_mv"], 3300, 30, "█", C_CYAN)
    b_mv2 = render_bar(t["zmpt2_mv"], 3300, 30, "█", C_CYAN)
    b_mvi = render_bar(t["zmct_mv"], 3300, 30, "█", C_AMBER)
    b_vpp = render_bar(t["mic_vpp"], 1500, 30, "█", C_RED)
    print(f"    ZMPT101B #1 : {t['zmpt1_mv']:6.1f} mV {b_mv1}")
    print(f"    ZMPT101B #2 : {t['zmpt2_mv']:6.1f} mV {b_mv2}")
    print(f"    ZMCT103C    : {t['zmct_mv']:6.1f} mV {b_mvi}")
    print(f"    MAX9814 Snd : {t['mic_vpp']:6.1f} mV Vpp {b_vpp}\n")

    print(f"  {C_BOLD}{C_PURPLE}[3. EXTERNAL ADS1115 16-BIT ADC (0 - 4096 mV)]{C_RESET}")
    b_a0 = render_bar(t["ads0_mv"], 4096, 30, "█", C_PURPLE)
    b_a1 = render_bar(t["ads1_mv"], 4096, 30, "█", C_PURPLE)
    b_a2 = render_bar(t["ads2_mv"], 4096, 30, "█", C_AMBER)
    print(f"    A0 (ZMPT1)  : {t['ads0_mv']:8.3f} mV {b_a0}")
    print(f"    A1 (ZMPT2)  : {t['ads1_mv']:8.3f} mV {b_a1}")
    print(f"    A2 (ZMCT)   : {t['ads2_mv']:8.3f} mV {b_a2}\n")

    print(f"  {C_BOLD}{C_BLUE}[4. DC POWER & ROTOR SPEED]{C_RESET}")
    b_ina1 = render_bar(t["ina1_w"], 120, 20, "█", C_BLUE)
    b_rpm  = render_bar(t["rpm"], 3000, 20, "█", C_GREEN)
    print(f"    INA226 #1   : {t['ina1_v']:5.2f} V | {t['ina1_a']:5.2f} A | {t['ina1_w']:6.2f} W {b_ina1}")
    print(f"    Rotor Speed : {t['rpm']:5.0f} RPM {b_rpm}")
    print(f"    ESP32 CPU   : {t['cpu_temp']:5.1f} °C\n")

    print(f"{C_BOLD}{C_CYAN}======================================================================{C_RESET}")
    print(f"  Press {C_BOLD}Ctrl+C{C_RESET} to close plotter window.")

def render_dashboard(port, log_filename):
    """Render real-time diagnostic table dashboard (WINDOW 1)."""
    clear_screen()
    t = telemetry

    print(f"{C_BOLD}{C_CYAN}======================================================================{C_RESET}")
    print(f"{C_BOLD}{C_CYAN}  [WINDOW 1] ESP32 WIND MONITOR REAL-TIME DASHBOARD                    {C_RESET}")
    print(f"{C_CYAN}  Port: {C_BOLD}{port}{C_RESET}{C_CYAN} | Baud: 115200 | Log: {os.path.basename(log_filename)}{C_RESET}")
    print(f"{C_CYAN}  [Local TCP Stream Server: Active on 127.0.0.1:8888 -> Window 2 Plotter]{C_RESET}")
    print(f"{C_BOLD}{C_CYAN}======================================================================{C_RESET}\n")

    print(f"  {C_BOLD}{C_PURPLE}[I2C BUS DISCOVERY SCAN]{C_RESET}")
    print(f"    Detected Devices : {C_GREEN}{t['i2c_scan']}{C_RESET}\n")

    print(f"  {C_BOLD}{C_YELLOW}[1. INTERNAL ESP32 ADC (eFuse Calibrated)]{C_RESET}")
    print(f"    ZMPT101B #1 (GPIO 34) : {C_GREEN}Raw: {int(t['raw_z1']):4d}{C_RESET} | {C_CYAN}{t['zmpt1_mv']:6.1f} mV{C_RESET}")
    print(f"    ZMPT101B #2 (GPIO 35) : {C_GREEN}Raw: {int(t['raw_z2']):4d}{C_RESET} | {C_CYAN}{t['zmpt2_mv']:6.1f} mV{C_RESET}")
    print(f"    ZMCT103C    (GPIO 32) : {C_GREEN}Raw: {int(t['raw_zi']):4d}{C_RESET} | {C_AMBER}{t['zmct_mv']:6.1f} mV{C_RESET}")
    print(f"    MAX9814     (GPIO 33) : {C_GREEN}Raw: {int(t['raw_mic']):4d}{C_RESET} | {C_CYAN}Bias: {t['mic_mv']:6.1f} mV{C_RESET} | Sound Level: {C_PURPLE}{t['mic_vpp']:6.1f} mV Vpp{C_RESET}\n")

    print(f"  {C_BOLD}{C_PURPLE}[2. EXTERNAL ADS1115 16-BIT ADC (0x48)]{C_RESET}")
    if t["ads_connected"] or t["ads0_mv"] > 0:
        print(f"    Channel A0 (ZMPT1) : {C_PURPLE}{t['ads0_mv']:8.3f} mV{C_RESET}     Channel A1 (ZMPT2) : {C_PURPLE}{t['ads1_mv']:8.3f} mV{C_RESET}")
        print(f"    Channel A2 (ZMCT)  : {C_AMBER}{t['ads2_mv']:8.3f} mV{C_RESET}     Channel A3 (Aux)   : {C_PURPLE}{t['ads3_mv']:8.3f} mV{C_RESET}\n")
    else:
        print(f"    Status: {C_YELLOW}NOT DETECTED (Using internal eFuse ADC){C_RESET}\n")

    print(f"  {C_BOLD}{C_BLUE}[3. INA226 DC POWER SENSORS]{C_RESET}")
    ina1_st = f"{C_GREEN}[OK]{C_RESET}" if t["ina1_status"] == "OK" else f"{C_RED}[MISSING]{C_RESET}"
    ina2_st = f"{C_GREEN}[OK]{C_RESET}" if t["ina2_status"] == "OK" else f"{C_RED}[MISSING]{C_RESET}"
    print(f"    INA226 #1 (0x44) : Voltage: {C_GREEN}{t['ina1_v']:5.2f} V{C_RESET} | Current: {C_GREEN}{t['ina1_a']:5.2f} A{C_RESET} | Power: {C_CYAN}{t['ina1_w']:6.2f} W{C_RESET} {ina1_st}")
    print(f"    INA226 #2 (0x45) : Voltage: {C_GREEN}{t['ina2_v']:5.2f} V{C_RESET} | Current: {C_GREEN}{t['ina2_a']:5.2f} A{C_RESET} | Power: {C_CYAN}{t['ina2_w']:6.2f} W{C_RESET} {ina2_st}\n")

    print(f"  {C_BOLD}{C_AMBER}[4. MECHANICAL SPEED & TEMPERATURES]{C_RESET}")
    t1_st = f"{C_GREEN}[OK]{C_RESET}" if t["temp1_status"] == "OK" else f"{C_RED}[DISCONNECTED]{C_RESET}"
    t2_st = f"{C_GREEN}[OK]{C_RESET}" if t["temp2_status"] == "OK" else f"{C_RED}[DISCONNECTED]{C_RESET}"
    print(f"    DS18B20 #1 : {C_GREEN}{t['temp1']:5.1f} °C{C_RESET} {t1_st}     DS18B20 #2 : {C_GREEN}{t['temp2']:5.1f} °C{C_RESET} {t2_st}")
    print(f"    ESP32 CPU  : {C_YELLOW}{t['cpu_temp']:5.1f} °C{C_RESET}            Rotor Speed: {C_GREEN}{t['rpm']:5.0f} RPM{C_RESET} ({t['pulses']} Pulses)\n")

    print(f"{C_BOLD}{C_CYAN}======================================================================{C_RESET}")
    print(f"  Press {C_BOLD}Ctrl+C{C_RESET} to exit dashboard.")

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
