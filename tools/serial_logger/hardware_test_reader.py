# =============================================================
#  hardware_test_reader.py — Legacy Wrapper to serial_logger.py
# =============================================================

import os
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

import serial_logger

def run_monitor(port=None, baud_rate=115200):
    serial_logger.run_logger(port, baud_rate, mode="dashboard")

def main():
    serial_logger.main()

if __name__ == "__main__":
    main()
