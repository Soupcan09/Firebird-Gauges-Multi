"""
serve_firmware.py -- tiny HTTP server that hosts the latest firmware
binary for OTA pickup by the Firebird gauges.

Usage from the project root:

    python scripts/serve_firmware.py

What it does:
- Copies build/ESP32-S3-Touch-LCD-2.1.bin -> serve/firmware.bin so the
  gauges always pull a stable filename (matches OTA_FIRMWARE_URL in
  ota_config.h).
- Starts an HTTP server on 0.0.0.0:8000 serving from the serve/ dir.
- Prints your machine's local IPs so you can paste the right one into
  ota_config.h on first setup.

Workflow:
1) idf.py build      # in another terminal
2) python scripts/serve_firmware.py
3) On each gauge: settings -> OTA -> wait ~30 sec -> auto-reboots

Stop with Ctrl+C.

Notes:
- Listens on all interfaces (0.0.0.0) so the gauges can reach it over
  WiFi. Windows Firewall may pop up on first run; allow Python on
  Private networks.
- No HTTPS. Fine for trusted home WiFi. If you want HTTPS, point
  ota_config.h at a real https:// URL behind nginx/Caddy/whatever.
"""
import http.server
import os
import shutil
import socket
import socketserver
import sys
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent
BUILD_BIN = PROJECT_ROOT / "build" / "ESP32-S3-Touch-LCD-2.1.bin"
SERVE_DIR = PROJECT_ROOT / "serve"
SERVE_BIN = SERVE_DIR / "firmware.bin"

PORT = 8000


def get_local_ips():
    """Return a list of likely-LAN IPv4 addresses (not 127.x)."""
    ips = []
    try:
        hostname = socket.gethostname()
        for _, _, _, _, addr in socket.getaddrinfo(hostname, None,
                                                    socket.AF_INET):
            ip = addr[0]
            if not ip.startswith("127.") and ip not in ips:
                ips.append(ip)
    except Exception:
        pass
    # also try the "connect to dummy" trick which picks the active iface
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        primary = s.getsockname()[0]
        s.close()
        if primary not in ips:
            ips.insert(0, primary)
    except Exception:
        pass
    return ips


def stage_firmware():
    SERVE_DIR.mkdir(exist_ok=True)
    if not BUILD_BIN.exists():
        sys.stderr.write(
            f"ERROR: build artifact not found at {BUILD_BIN}\n"
            f"  Run 'idf.py build' first.\n"
        )
        sys.exit(1)
    shutil.copy2(BUILD_BIN, SERVE_BIN)
    sz_kb = SERVE_BIN.stat().st_size // 1024
    print(f"Staged {BUILD_BIN.name} -> {SERVE_BIN}  ({sz_kb} KB)")


def main():
    stage_firmware()

    ips = get_local_ips()
    print()
    print("=" * 64)
    print("Firmware server running.")
    print(f"  Serving:   {SERVE_DIR}/  (firmware.bin)")
    print(f"  Port:      {PORT}")
    print("  URLs to put in ota_config.h (pick one your gauges can reach):")
    for ip in ips:
        print(f"      http://{ip}:{PORT}/firmware.bin")
    if not ips:
        print("      (could not auto-detect local IP -- run 'ipconfig'/'ip addr')")
    print("=" * 64)
    print()
    print("Press CTRL+C to stop.")
    print()

    os.chdir(SERVE_DIR)
    handler = http.server.SimpleHTTPRequestHandler
    # Reuse address so re-runs don't hit a TIME_WAIT block
    with socketserver.TCPServer(("0.0.0.0", PORT), handler,
                                 bind_and_activate=False) as httpd:
        httpd.allow_reuse_address = True
        httpd.server_bind()
        httpd.server_activate()
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\nStopped.")


if __name__ == "__main__":
    main()
