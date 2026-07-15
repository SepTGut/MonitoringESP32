# =============================================================
#  serial_reader.py — ESP32 Serial Monitor & Logger
#  Reads serial output from ESP32, prints it, and saves it
#  to a timestamped log file.
#
#  Requirements: pip install pyserial
# =============================================================

import os
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

def find_ports():
    """List all available serial ports."""
    ports = list(serial.tools.list_ports.comports())
    return ports

def select_port(ports):
    """Prompt user to select a serial port."""
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
    print("====================================================")
    print("  ESP32 Wind Monitor Serial Reader & Logger")
    print("====================================================")
    
    ports = find_ports()
    port = select_port(ports)
    baud_rate = 115200
    
    # Create logs directory if it doesn't exist
    log_dir = "logs"
    if not os.path.exists(log_dir):
        os.makedirs(log_dir)
        
    # Create timestamped file name
    timestamp = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    log_filename = os.path.join(log_dir, f"wind_monitor_{timestamp}.log")
    
    print(f"\nConnecting to {port} at {baud_rate} baud...")
    print(f"Logging output to: {os.path.abspath(log_filename)}")
    print("Press Ctrl+C to stop logging.\n")
    
    try:
        # Open serial port
        ser = serial.Serial(port, baud_rate, timeout=1)
        # Toggle DTR/RTS to reset the ESP32 (simulates opening serial monitor in IDE)
        ser.dtr = False
        ser.rts = False
        time.sleep(0.1)
        ser.dtr = True
        ser.rts = True
        
        with open(log_filename, "a", encoding="utf-8") as log_file:
            # Write session header
            header = f"\n--- LOGGING SESSION START: {datetime.now()} ---\n"
            log_file.write(header)
            log_file.flush()
            
            while True:
                if ser.in_waiting > 0:
                    line_bytes = ser.readline()
                    try:
                        # Decode with utf-8, replace errors to prevent crashes on garbage data
                        line = line_bytes.decode("utf-8", errors="replace")
                    except Exception:
                        line = str(line_bytes)
                        
                    # Format log line with local timestamp
                    time_str = datetime.now().strftime("%H:%M:%S.%f")[:-3]
                    log_line = f"[{time_str}] {line}"
                    
                    # Print to terminal
                    sys.stdout.write(line)
                    sys.stdout.flush()
                    
                    # Save to log file
                    log_file.write(line)
                    log_file.flush()
                else:
                    time.sleep(0.01)  # Yield CPU
                    
    except serial.SerialException as e:
        print(f"\nSerial Error: {e}")
    except KeyboardInterrupt:
        print("\nLogging stopped by user.")
    finally:
        if 'ser' in locals() and ser.is_open:
            ser.close()
            print("Serial port closed.")

if __name__ == "__main__":
    main()
