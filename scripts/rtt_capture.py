#!/usr/bin/env python3
"""Capture RTT output from nRF54L15 using pyocd Python API."""
import sys
import time
import signal

from pyocd.core.helpers import ConnectHelper
from pyocd.debug.rtt import RTTControlBlock

PROBE_UID = "8ABD0345"
RTT_ADDRESS = 0x20000000
DURATION = float(sys.argv[1]) if len(sys.argv) > 1 else 15.0

running = True
def _stop(sig, frame):
    global running
    running = False
signal.signal(signal.SIGINT, _stop)
signal.signal(signal.SIGTERM, _stop)

with ConnectHelper.session_with_chosen_probe(
    unique_id=PROBE_UID,
    target_override="nrf54l",
    connect_mode="attach",
    options={"logging.file_log_level": "warning"},
) as session:
    board = session.board
    target = board.target
    target.resume()

    rtt = RTTControlBlock.from_target(target, address=RTT_ADDRESS, size=0)
    rtt.start()

    deadline = time.monotonic() + DURATION
    print(f"[RTT] Capturing for {DURATION:.0f}s... (Ctrl-C to stop early)", flush=True)
    while running and time.monotonic() < deadline:
        data = rtt.up_channels[0].read()
        if data:
            sys.stdout.write(data.decode("utf-8", errors="replace"))
            sys.stdout.flush()
        else:
            time.sleep(0.05)

    print("\n[RTT] Done.", flush=True)
