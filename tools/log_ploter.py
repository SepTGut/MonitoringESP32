# =============================================================
#  log_ploter.py — ESP32 Raw Sensor Serial Real-Time Plotter
#
#  Shortcut entrypoint to launch tools/serial_logger/log_ploter.py
#  Usage: python tools/log_ploter.py [--port COM3] [--baud 115200]
# =============================================================

import os
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
LOGGER_DIR = os.path.join(SCRIPT_DIR, "serial_logger")

if LOGGER_DIR not in sys.path:
    sys.path.insert(0, LOGGER_DIR)

import log_ploter

if __name__ == "__main__":
    log_ploter.main()
