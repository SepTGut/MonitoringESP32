# =============================================================
#  erase_and_monitor.py — ESP32 Flash Eraser & Custom Serial Monitor
#
#  1. Automatically detects or selects ESP32 serial COM port.
#  2. Erases all flash memory (firmware, NVS, and LittleFS).
#  3. Optionally compiles/uploads fresh firmware or hardware test.
#  4. Launches the custom real-time color-coded serial monitor.
#
#  Requirements: pip install pyserial esptool
#  Usage: python tools/erase_and_monitor.py [--port COM3] [--baud 115200]
# =============================================================

import os
import sys
import time
import argparse
import subprocess
from datetime import datetime

# Attempt to import pyserial and esptool
try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("[Error] 'pyserial' package is not installed.")
    print("Installing pyserial automatically...")
    subprocess.check_call([sys.executable, "-m", "pip", "install", "pyserial"])
    import serial
    import serial.tools.list_ports

# Add serial_logger path so we can import serial_logger directly
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
LOGGER_DIR = os.path.join(SCRIPT_DIR, "serial_logger")
if LOGGER_DIR not in sys.path:
    sys.path.insert(0, LOGGER_DIR)

# Terminal ANSI Color Constants
C_RESET  = "\033[0m"
C_BOLD   = "\033[1m"
C_RED    = "\033[31m"
C_GREEN  = "\033[32m"
C_YELLOW = "\033[33m"
C_BLUE   = "\033[34m"
C_CYAN   = "\033[36m"

def get_platformio_exe():
    """Find local PlatformIO executable."""
    home_pio = os.path.expanduser("~/.platformio/penv/Scripts/pio.exe")
    if os.path.exists(home_pio):
        return home_pio
    return "pio"

def list_serial_ports():
    """Returns a list of available serial COM ports."""
    return list(serial.tools.list_ports.comports())

def auto_select_port(requested_port=None):
    """Select requested port or prompt user from list."""
    ports = list_serial_ports()
    
    if requested_port:
        return requested_port

    if not ports:
        print(f"{C_RED}[Error] No serial ports found. Please connect ESP32 USB cable and try again.{C_RESET}")
        sys.exit(1)

    if len(ports) == 1:
        selected = ports[0].device
        print(f"{C_GREEN}[Auto-detected] Using serial port: {C_BOLD}{selected}{C_RESET} ({ports[0].description})")
        return selected

    print(f"\n{C_CYAN}Available Serial Ports:{C_RESET}")
    for idx, p in enumerate(ports):
        print(f"  [{idx}] {C_BOLD}{p.device}{C_RESET} - {p.description}")

    while True:
        try:
            choice = input(f"\nSelect port index (0-{len(ports)-1}, default 0): ").strip()
            if not choice:
                return ports[0].device
            idx = int(choice)
            if 0 <= idx < len(ports):
                return ports[idx].device
            print(f"{C_YELLOW}Please select a valid index between 0 and {len(ports)-1}{C_RESET}")
        except (ValueError, KeyboardInterrupt):
            return ports[0].device

def erase_esp32_flash(port):
    """Erase all ESP32 flash memory using esptool or PlatformIO."""
    print(f"\n{C_YELLOW}===================================================={C_RESET}")
    print(f"{C_YELLOW}  STEP 1: ERASING ESP32 FLASH MEMORY ({port}){C_RESET}")
    print(f"{C_YELLOW}===================================================={C_RESET}")
    print(f"Erasing program, NVS config, and LittleFS filesystem...")
    
    # Method 1: Try python esptool module
    cmd_esptool = [sys.executable, "-m", "esptool", "--chip", "esp32", "--port", port, "erase_flash"]
    try:
        res = subprocess.run(cmd_esptool, check=True)
        if res.returncode == 0:
            print(f"\n{C_GREEN}✔ Flash erase complete! ESP32 completely cleared.{C_RESET}\n")
            return True
    except (subprocess.CalledProcessError, FileNotFoundError):
        print(f"{C_YELLOW}[Notice] esptool module not directly available; falling back to PlatformIO erase...{C_RESET}")

    # Method 2: PlatformIO fallback
    pio_exe = get_platformio_exe()
    cmd_pio = [pio_exe, "run", "-t", "erase", "--upload-port", port]
    try:
        res = subprocess.run(cmd_pio, check=True)
        if res.returncode == 0:
            print(f"\n{C_GREEN}✔ Flash erase complete!{C_RESET}\n")
            return True
    except subprocess.CalledProcessError:
        print(f"\n{C_RED}✖ Erase flash failed. Please check ESP32 connection or BOOT button.{C_RESET}")
        return False

def generate_assets():
    """Generates embedded web assets header before building firmware."""
    root_dir = os.path.dirname(SCRIPT_DIR)
    gen_script = os.path.join(SCRIPT_DIR, "generate_web_assets.py")
    if os.path.exists(gen_script):
        try:
            print(f"{C_CYAN}[Assets] Generating pre-compressed web assets...{C_RESET}")
            subprocess.run([sys.executable, gen_script], check=True)
        except Exception as e:
            print(f"{C_YELLOW}[Warning] generate_web_assets.py error: {e}{C_RESET}")

def prompt_reupload(port):
    """Optionally offer to re-upload firmware or test sketch before launching serial monitor."""
    print(f"{C_CYAN}Select Post-Erase Action:{C_RESET}")
    print(f"  [1] Open Custom Serial Monitor immediately (device remains erased/blank)")
    print(f"  [2] Build & Upload Main Firmware (Production) + LittleFS filesystem")
    print(f"  [3] Build & Upload Hardware Diagnostic Test (test/hardware_test)")
    print(f"  [4] Build & Upload Hardware Test + Launch Raw Serial Plotter")
    
    choice = input(f"\nEnter choice [1/2/3/4] (default 1): ").strip()
    pio_exe = get_platformio_exe()

    if choice == "2":
        generate_assets()
        print(f"\n{C_YELLOW}[Uploading Main Firmware & Filesystem...]{C_RESET}")
        subprocess.run([pio_exe, "run", "-t", "upload", "--upload-port", port], check=True)
        subprocess.run([pio_exe, "run", "-t", "uploadfs", "--upload-port", port], check=True)
        print(f"{C_GREEN}✔ Firmware & LittleFS uploaded successfully!{C_RESET}")
    elif choice in ["3", "4"]:
        print(f"\n{C_YELLOW}[Uploading Hardware Sensor Test Firmware...]{C_RESET}")
        root_dir = os.path.dirname(SCRIPT_DIR)
        hw_dir = os.path.join(root_dir, "test", "hardware_test")
        subprocess.run([pio_exe, "run", "-d", hw_dir, "-t", "upload", "--upload-port", port], check=True)
        print(f"{C_GREEN}✔ Hardware test uploaded successfully!{C_RESET}")
        if choice == "4":
            return "plot"
    return "default"

def launch_custom_serial_monitor(port, baud_rate=115200, is_test=False, is_plot=False, is_single=False):
    """Launch unified custom color-coded serial dashboard monitor or raw plotter."""
    import importlib
    mode = "plot" if is_plot else ("dashboard" if is_single else "dual")

    try:
        s_logger = importlib.import_module("serial_logger")
        s_logger.run_logger(port, baud_rate, mode=mode)
        return
    except Exception as e:
        print(f"{C_YELLOW}[Notice] serial_logger module error ({e}), launching fallback raw monitor...{C_RESET}")

    # Fallback to direct raw colorized reader
    try:
        ser = serial.Serial(port, baud_rate, timeout=1)
        ser.dtr = False; ser.rts = False; time.sleep(0.1)
        ser.dtr = True; ser.rts = True

        print(f"{C_GREEN}Connected to {port} at {baud_rate} baud. Press Ctrl+C to exit.{C_RESET}\n")
        while True:
            if ser.in_waiting > 0:
                line = ser.readline().decode("utf-8", errors="replace")
                if "[OK]" in line:
                    print(f"{C_GREEN}{line.strip()}{C_RESET}")
                elif "WARNING" in line or "Warning" in line or "ERR" in line:
                    print(f"{C_YELLOW}{line.strip()}{C_RESET}")
                elif "ERROR" in line or "Error" in line or "FAIL" in line:
                    print(f"{C_RED}{line.strip()}{C_RESET}")
                else:
                    print(line.strip())
            else:
                time.sleep(0.01)
    except serial.SerialException as se:
        print(f"{C_RED}Serial port error: {se}{C_RESET}")
    except KeyboardInterrupt:
        print(f"\n{C_CYAN}Exiting serial monitor...{C_RESET}")
    finally:
        if 'ser' in locals() and ser.is_open:
            ser.close()

def main():
    parser = argparse.ArgumentParser(description="ESP32 Erase Flash & Custom Serial Monitor")
    parser.add_argument("--port", help="ESP32 serial COM port (e.g. COM3 or /dev/ttyUSB0)")
    parser.add_argument("--baud", type=int, default=115200, help="Serial baud rate (default: 115200)")
    parser.add_argument("--skip-erase", action="store_true", help="Skip flash erase step")
    parser.add_argument("--test", action="store_true", help="Automatically upload hardware test firmware after erase")
    parser.add_argument("--upload", action="store_true", help="Automatically upload main production firmware + LittleFS after erase")
    parser.add_argument("--plot", action="store_true", help="Launch live raw value serial plotter")
    parser.add_argument("--single", action="store_true", help="Launch single dashboard window without opening 2nd plotter window")
    args = parser.parse_args()

    print(f"{C_BOLD}{C_CYAN}")
    print("====================================================")
    print("  ESP32 Wind Monitor — Erase Flash & Serial Monitor ")
    print("====================================================")
    print(f"{C_RESET}")

    port = auto_select_port(args.port)
    action = "default"

    if not args.skip_erase:
        success = erase_esp32_flash(port)
        if not success:
            sys.exit(1)
        
        if args.test:
            print(f"\n{C_YELLOW}[Auto-Uploading Hardware Sensor Test Firmware...]{C_RESET}")
            pio_exe = get_platformio_exe()
            root_dir = os.path.dirname(SCRIPT_DIR)
            hw_dir = os.path.join(root_dir, "test", "hardware_test")
            subprocess.run([pio_exe, "run", "-d", hw_dir, "-t", "upload", "--upload-port", port], check=True)
            print(f"{C_GREEN}✔ Hardware test uploaded successfully!{C_RESET}")
        elif args.upload:
            generate_assets()
            print(f"\n{C_YELLOW}[Auto-Uploading Main Production Firmware...]{C_RESET}")
            pio_exe = get_platformio_exe()
            subprocess.run([pio_exe, "run", "-t", "upload", "--upload-port", port], check=True)
            subprocess.run([pio_exe, "run", "-t", "uploadfs", "--upload-port", port], check=True)
            print(f"{C_GREEN}✔ Firmware & LittleFS uploaded successfully!{C_RESET}")
        else:
            action = prompt_reupload(port)
    
    is_plot = args.plot or (action == "plot")
    launch_custom_serial_monitor(port, args.baud, is_test=args.test, is_plot=is_plot, is_single=args.single)

if __name__ == "__main__":
    main()
