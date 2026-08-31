# =============================================================
#  flash_esp32.py — Robust ESP32 Flash Tool with Boot Mode Helper
# =============================================================

import os
import sys
import time
import subprocess

def main():
    port = sys.argv[1] if len(sys.argv) > 1 else "COM3"
    print("=" * 60)
    print(f"  ESP32 FIRMWARE FLASHER -> TARGET PORT: {port}")
    print("=" * 60)
    print("\n[NOTE] If upload stops at 'Connecting...', HOLD DOWN the 'BOOT' button on ESP32!")
    print("Starting in 2 seconds...\n")
    time.sleep(2)

    cmd = [
        sys.executable, "-m", "platformio", "run",
        "-t", "upload",
        "--upload-port", port
    ]

    env = os.environ.copy()
    env["PYTHONIOENCODING"] = "utf-8"

    result = subprocess.run(cmd, env=env)
    if result.returncode == 0:
        print("\n" + "=" * 60)
        print("  [SUCCESS] Firmware uploaded successfully!")
        print("=" * 60)
    else:
        print("\n" + "=" * 60)
        print("  [TIP] Please HOLD the physical BOOT button on your ESP32")
        print("        and run: python tools/flash_esp32.py COM3")
        print("=" * 60)

if __name__ == "__main__":
    main()
