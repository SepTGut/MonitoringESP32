import os
import sys
import time
import serial
from datetime import datetime

def capture_live(duration_sec=15, port="COM3", baud=115200):
    logs_dir = os.path.join(os.path.dirname(__file__), "serial_logger", "logs")
    os.makedirs(logs_dir, exist_ok=True)
    
    timestamp = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    log_file = os.path.join(logs_dir, f"serial_logger_{timestamp}.log")
    
    print(f"[Capture] Connecting to {port} @ {baud} baud...")
    ser = serial.Serial(port, baud, timeout=1)
    ser.dtr = False; ser.rts = False; time.sleep(0.1)
    ser.dtr = True; ser.rts = True; time.sleep(0.2)
    
    # Send wake-up and force continuous streaming command
    ser.write(b"PING\n")
    time.sleep(0.1)
    ser.write(b"AUTOLOG OFF\n")
    time.sleep(0.1)
    ser.write(b"TEXT\n")
    
    print(f"[Capture] Recording live telemetry to: {log_file}")
    start_time = time.time()
    lines_recorded = 0
    
    with open(log_file, "w", encoding="utf-8") as f:
        f.write(f"=== Live Test Session Started at {datetime.now()} ===\n")
        while time.time() - start_time < duration_sec:
            if ser.in_waiting > 0:
                line = ser.readline().decode("utf-8", errors="replace")
                try:
                    sys.stdout.write(line)
                    sys.stdout.flush()
                except Exception:
                    pass
                f.write(line)
                f.flush()
                lines_recorded += 1
            else:
                time.sleep(0.02)
                
    # Restore autolog on
    ser.write(b"AUTOLOG ON\n")
    time.sleep(0.1)
    ser.close()
    
    print(f"\n[Capture] Finished! Recorded {lines_recorded} lines in {duration_sec}s.")
    return log_file

if __name__ == "__main__":
    port_arg = sys.argv[1] if len(sys.argv) > 1 else "COM3"
    capture_live(duration_sec=12, port=port_arg)
