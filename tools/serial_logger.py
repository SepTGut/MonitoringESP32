# =============================================================
#  serial_logger.py — ESP32 Unified Serial Dashboard & Waveform Plotter
#
#  Shortcut entrypoint to launch tools/serial_logger/serial_logger.py
#  Usage: python tools/serial_logger.py [--port COM3] [--plot] [--mode dashboard|plot]
# =============================================================

import os
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
LOGGER_DIR = os.path.join(SCRIPT_DIR, "serial_logger")

if LOGGER_DIR not in sys.path:
    sys.path.insert(0, LOGGER_DIR)

import serial_logger

if __name__ == "__main__":
    serial_logger.main()
