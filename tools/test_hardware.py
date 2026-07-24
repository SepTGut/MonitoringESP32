# =============================================================
#  test_hardware.py — Erase Flash & Run Hardware Sensor Test
#
#  1. Auto-detects/selects ESP32 COM port.
#  2. Erases all ESP32 flash memory.
#  3. Compiles & uploads the Hardware Sensor Test firmware (test/hardware_test).
#  4. Opens the Serial Monitor (115200 baud) to view real-time diagnostics.
#
#  Usage: python tools/test_hardware.py [--port COM3] [--skip-erase]
# =============================================================

import os
import sys
import subprocess

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
ERASE_SCRIPT = os.path.join(SCRIPT_DIR, "erase_and_monitor.py")

def main():
    # Pass --test flag to erase_and_monitor.py along with any CLI arguments
    cmd = [sys.executable, ERASE_SCRIPT, "--test"] + sys.argv[1:]
    try:
        subprocess.run(cmd, check=True)
    except KeyboardInterrupt:
        print("\nHardware test monitor exited.")
    except subprocess.CalledProcessError as e:
        sys.exit(e.returncode)

if __name__ == "__main__":
    main()
