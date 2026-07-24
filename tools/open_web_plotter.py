# =============================================================
#  open_web_plotter.py — Auto-Launch Web Serial Plotter
#
#  Launches tools/web_serial_plotter/index.html directly in the
#  default Web Browser (Google Chrome / Microsoft Edge).
#
#  Usage: python tools/open_web_plotter.py
# =============================================================

import os
import sys
import webbrowser

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
HTML_PATH = os.path.join(SCRIPT_DIR, "web_serial_plotter", "index.html")

def main():
    if not os.path.exists(HTML_PATH):
        print(f"[Error] Web Serial Plotter HTML file not found at: {HTML_PATH}")
        sys.exit(1)

    file_url = f"file:///{os.path.abspath(HTML_PATH).replace('\\', '/')}"
    print(f"Opening Web Serial Plotter in default web browser...")
    print(f"URL: {file_url}")
    
    webbrowser.open(file_url)

if __name__ == "__main__":
    main()
