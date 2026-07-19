# =============================================================
#  serial_reader.py — ESP32 Serial Dashboard & Logger
#  Reads serial logs, parses real-time metrics, and updates
#  a clean, color-coded console UI in-place.
#
#  Requirements: pip install pyserial
# =============================================================

import os
import re
import sys
import time
from datetime import datetime

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("Error: 'pyserial' package is not installed.")
    print("Please install it using: pip install pyserial")
    sys.exit(1)

# Global sensor data state
data = {
    "ac_voltage": 0.0,
    "ac_voltage2": 0.0,
    "ac_current": 0.0,
    "ac_power": 0.0,
    "dc1_voltage": 0.0,
    "dc1_current": 0.0,
    "dc1_power": 0.0,
    "dc2_voltage": 0.0,
    "dc2_current": 0.0,
    "dc2_power": 0.0,
    "temp1": 0.0,
    "temp2": 0.0,
    "temp_esp": 0.0,
    "rpm": 0
}

# Regex patterns for parsing log formats
re_val = re.compile(r"Voltage:\s*([\d.-]+)V|Current:\s*([\d.-]+)A|Power:\s*([\d.-]+)W|V2 \(raw\):\s*([\d.-]+)V")
re_temp = re.compile(r"Temperature:\s*([\d.-]+)°C\s*/\s*([\d.-]+)°C\s*\(Internal CPU:\s*([\d.-]+)°C\)")
re_temp_fallback = re.compile(r"Temperature:\s*([\d.-]+)°C\s*/\s*([\d.-]+)°C")
re_rpm = re.compile(r"RPM:\s*(\d+)")

def parse_line(line, state):
    """Parse a single line from the ESP32 log and update data dictionary."""
    global data
    
    # Clean the line
    cleaned = line.strip()
    
    # Section transitions
    if cleaned == "AC":
        state["section"] = "AC"
        return
    elif cleaned == "DC INA226 #1":
        state["section"] = "DC1"
        return
    elif cleaned == "DC INA226 #2":
        state["section"] = "DC2"
        return
    elif cleaned.startswith("==="):
        state["section"] = "IDLE"
        return
        
    # Match standard value readings (Voltage, Current, Power, V2)
    val_match = re_val.search(cleaned)
    if val_match:
        groups = val_match.groups()
        section = state["section"]
        
        # groups[0] = Voltage, groups[1] = Current, groups[2] = Power, groups[3] = V2
        if groups[0] is not None:
            val = float(groups[0])
            if section == "AC": data["ac_voltage"] = val
            elif section == "DC1": data["dc1_voltage"] = val
            elif section == "DC2": data["dc2_voltage"] = val
        elif groups[1] is not None:
            val = float(groups[1])
            if section == "AC": data["ac_current"] = val
            elif section == "DC1": data["dc1_current"] = val
            elif section == "DC2": data["dc2_current"] = val
        elif groups[2] is not None:
            val = float(groups[2])
            if section == "AC": data["ac_power"] = val
            elif section == "DC1": data["dc1_power"] = val
            elif section == "DC2": data["dc2_power"] = val
        elif groups[3] is not None:
            data["ac_voltage2"] = float(groups[3])
        return

    # Match Temperature readings (3 temperatures: ext1, ext2, internal CPU)
    temp_match = re_temp.search(cleaned)
    if temp_match:
        data["temp1"] = float(temp_match.group(1))
        data["temp2"] = float(temp_match.group(2))
        data["temp_esp"] = float(temp_match.group(3))
        return

    # Fallback: 2-temperature format (legacy, no CPU temp)
    temp_fb = re_temp_fallback.search(cleaned)
    if temp_fb:
        data["temp1"] = float(temp_fb.group(1))
        data["temp2"] = float(temp_fb.group(2))
        return

    # Match RPM speed
    rpm_match = re_rpm.search(cleaned)
    if rpm_match:
        data["rpm"] = int(rpm_match.group(1))
        return

def render_ui(port, log_path):
    """Draw the real-time sensor metrics dashboard in-place using ANSI escape codes."""
    # Reset cursor position to top-left instead of full clear to prevent screen flickering
    sys.stdout.write("\033[H")
    
    # Color definition helpers
    C_BLUE = "\033[1;34m"
    C_CYAN = "\033[1;36m"
    C_GREEN = "\033[1;32m"
    C_AMBER = "\033[1;33m"
    C_PURPLE = "\033[1;35m"
    C_RED = "\033[1;31m"
    C_RESET = "\033[0m"
    
    # Calculate colors/indicators based on loads
    ac_pwr_color = C_GREEN if data["ac_power"] > 0 else C_RESET
    dc1_pwr_color = C_GREEN if data["dc1_power"] > 0 else C_RESET
    dc2_pwr_color = C_GREEN if data["dc2_power"] > 0 else C_RESET

    # CPU temperature warning color
    cpu_temp = data["temp_esp"]
    if cpu_temp >= 70:
        cpu_color = C_RED
    elif cpu_temp >= 55:
        cpu_color = C_AMBER
    else:
        cpu_color = C_GREEN
    
    print(f"{C_CYAN}======================================================================{C_RESET}")
    print(f"           {C_BLUE}WIND TURBINE GENERATOR REAL-TIME MONITOR (CLI){C_RESET}")
    print(f"{C_CYAN}======================================================================{C_RESET}")
    print(f"  [STATUS] Connected to: {C_AMBER}{port}{C_RESET} | [LOG] {log_path}")
    print(f"  [UPTIME] Local time  : {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    print(f"{C_CYAN}----------------------------------------------------------------------{C_RESET}")
    
    # 1. AC Generator Section
    print(f"  {C_PURPLE}AC GENERATOR OUTPUT (Before Rectifier){C_RESET}")
    print(f"    RMS Voltage : {C_BLUE}{data['ac_voltage']:>6.1f} V{C_RESET}     RMS Current : {C_BLUE}{data['ac_current']:>6.2f} A{C_RESET}")
    print(f"    Est. Power  : {ac_pwr_color}{data['ac_power']:>6.1f} W{C_RESET}     Aux Voltage : {C_BLUE}{data['ac_voltage2']:>6.1f} V{C_RESET}")
    print()
    
    # 2. DC Output Section
    print(f"  {C_BLUE}DC RECTIFIED OUTPUTS (After MPPT / Charger){C_RESET}")
    print(f"    {C_CYAN}[Channel #1 — Main Load]{C_RESET}")
    print(f"      Voltage : {C_GREEN}{data['dc1_voltage']:>6.2f} V{C_RESET}     Current : {C_GREEN}{data['dc1_current']:>6.2f} A{C_RESET}     Power : {dc1_pwr_color}{data['dc1_power']:>6.2f} W{C_RESET}")
    print(f"    {C_CYAN}[Channel #2 — Auxiliary]{C_RESET}")
    print(f"      Voltage : {C_GREEN}{data['dc2_voltage']:>6.2f} V{C_RESET}     Current : {C_GREEN}{data['dc2_current']:>6.2f} A{C_RESET}     Power : {dc2_pwr_color}{data['dc2_power']:>6.2f} W{C_RESET}")
    print()
    
    # 3. Mechanical & Env Section
    print(f"  {C_AMBER}MECHANICAL SPEED & TEMPERATURE{C_RESET}")
    print(f"    Rotor Speed : {C_GREEN}{data['rpm']:>6d} RPM{C_RESET}")
    print(f"    External    : Sensor 1: {C_GREEN}{data['temp1']:>5.1f} °C{C_RESET}   |   Sensor 2: {C_GREEN}{data['temp2']:>5.1f} °C{C_RESET}")
    print(f"    CPU (ESP32) : {cpu_color}{data['temp_esp']:>5.1f} °C{C_RESET}")
    print(f"{C_CYAN}======================================================================{C_RESET}")
    print("  Press Ctrl+C to disconnect and exit.")
    
    # Force flush output
    sys.stdout.flush()

def find_ports():
    ports = list(serial.tools.list_ports.comports())
    return ports

def select_port(ports):
    if not ports:
        print("No serial ports found. Please connect your ESP32 and try again.")
        sys.exit(1)
        
    print("\nAvailable Serial Ports:")
    for idx, port in enumerate(ports):
        print(f"[{idx}] {port.device} - {port.description}")
        
    while True:
        try:
            val = input("\nSelect port index (default 0): ").strip()
            if not val:
                return ports[0].device
            idx = int(val)
            if 0 <= idx < len(ports):
                return ports[idx].device
            else:
                print(f"Please select a number between 0 and {len(ports)-1}")
        except ValueError:
            print("Invalid input. Please enter a number.")

def main():
    # Set console screen height/width standard (clears the window initially)
    os.system("cls" if os.name == "nt" else "clear")
    
    print("====================================================")
    print("  ESP32 Wind Monitor Serial Dashboard & Logger")
    print("====================================================")
    
    ports = find_ports()
    port = select_port(ports)
    baud_rate = 115200
    
    log_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "logs")
    if not os.path.exists(log_dir):
        os.makedirs(log_dir)
        
    timestamp = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    log_filename = os.path.join(log_dir, f"wind_monitor_{timestamp}.log")
    
    print(f"\nConnecting to {port}...")
    time.sleep(0.5)
    
    # Initialize console redraw window
    os.system("cls" if os.name == "nt" else "clear")
    
    try:
        ser = serial.Serial(port, baud_rate, timeout=1)
        # Reset the ESP32 to start log session fresh
        ser.dtr = False
        ser.rts = False
        time.sleep(0.1)
        ser.dtr = True
        ser.rts = True
        
        state = {"section": "IDLE"}
        
        with open(log_filename, "a", encoding="utf-8") as log_file:
            log_file.write(f"\n--- LOGGING SESSION START: {datetime.now()} ---\n")
            log_file.flush()
            
            # Initial drawing
            render_ui(port, log_filename)
            
            while True:
                if ser.in_waiting > 0:
                    line_bytes = ser.readline()
                    try:
                        line = line_bytes.decode("utf-8", errors="replace")
                    except Exception:
                        line = str(line_bytes)
                    
                    # Log raw data to file in background
                    log_file.write(line)
                    log_file.flush()
                    
                    # Parse data metrics
                    parse_line(line, state)
                    
                    # Refresh the UI dynamically on every block boundary
                    if "======================" in line:
                        render_ui(port, log_filename)
                else:
                    time.sleep(0.01)
                    
    except serial.SerialException as e:
        print(f"\nSerial Error: {e}")
    except KeyboardInterrupt:
        # Move cursor to end of UI table on exit
        print("\n\nDisconnecting...")
    finally:
        if 'ser' in locals() and ser.is_open:
            ser.close()
            print("Serial port closed.")

if __name__ == "__main__":
    main()
