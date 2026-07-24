# =============================================================
#  log_ploter.py — Legacy Wrapper to serial_logger.py (--mode plot)
# =============================================================

import os
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

import serial_logger

def run_plotter(port=None, baud_rate=115200):
    serial_logger.run_logger(port, baud_rate, mode="plot")

def main():
    serial_logger.run_logger(mode="plot")

if __name__ == "__main__":
    main()
