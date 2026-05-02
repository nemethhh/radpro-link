#!/usr/bin/env python3
"""
Send RadPro protocol commands to the RadPro-Link device via BLE NUS.

Architecture:
  PC  ──BLE/NUS──>  RadPro-Link (nRF54L15)  ──UART21──>  Rad Pro detector
  PC  <──BLE/NUS──  RadPro-Link (nRF54L15)  <──UART21──  Rad Pro detector

NUS UUIDs (Nordic UART Service):
  Service:  6E400001-B5A3-F393-E0A9-E50E24DCCA9E
  TX char:  6E400002 — write: PC → RadPro-Link UART
  RX char:  6E400003 — notify: UART responses → PC
"""

import asyncio
import sys
from bleak import BleakScanner, BleakClient

DEVICE_NAME = "RadPro-Link"
NUS_SERVICE  = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
NUS_TX_UUID  = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
NUS_RX_UUID  = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

RESPONSE_TIMEOUT = 4.0   # seconds to wait for a complete response
DRAIN_TIME       = 1.0   # seconds to drain unsolicited data before first command

COMMANDS = [
    "GET deviceId",
    "GET deviceBatteryVoltage",
    "GET deviceTime",
    "GET tubeType",
    "GET tubePulseCount",
    "GET tubeRate",
    "GET tubeSensitivity",
    "GET tubeDeadTime",
    "GET tubeDeadTimeCompensation",
    "GET randomData",
]


async def find_device(timeout: float = 10.0):
    print(f"Scanning for '{DEVICE_NAME}' ({int(timeout)}s)...")
    discovered = await BleakScanner.discover(timeout=timeout, return_adv=True)
    for addr, (device, adv) in discovered.items():
        uuids = [u.lower() for u in (adv.service_uuids if adv else [])]
        if (device.name or "").lower() == DEVICE_NAME.lower() or NUS_SERVICE in uuids:
            rssi = adv.rssi if adv else "?"
            print(f"Found: {addr}  RSSI={rssi}  name={device.name!r}")
            return device
    return None


def parse_lines(raw: bytes) -> list[str]:
    """Extract printable RadPro response lines from raw bytes."""
    lines = []
    for part in raw.replace(b'\r\n', b'\n').replace(b'\r', b'\n').split(b'\n'):
        # Keep only printable ASCII
        text = ''.join(chr(b) if 32 <= b < 127 else '' for b in part).strip()
        if text:
            lines.append(text)
    return lines


async def run_tests(address: str) -> bool:
    raw_buf = bytearray()
    data_event = asyncio.Event()

    def on_notify(char, data: bytes):
        raw_buf.extend(data)
        data_event.set()

    print(f"\nConnecting to {address}...")
    async with BleakClient(address) as client:
        print("Connected.")

        # Firmware requires BT_SECURITY_L2 (encrypted + authenticated) before
        # forwarding data to UART. The device auto-confirms within its 1-min pairing window.
        print("Pairing (Just Works — no passkey, gives BT_SECURITY_L2)...")
        try:
            await client.pair(protection_level=1)
            print("Paired.")
        except Exception as e:
            print(f"Pairing: {e}")

        await client.start_notify(NUS_RX_UUID, on_notify)
        print(f"Draining unsolicited startup data ({DRAIN_TIME:.0f}s)...")
        await asyncio.sleep(DRAIN_TIME)
        raw_buf.clear()
        data_event.clear()
        print()

        passed = 0
        failed = 0
        errors = 0

        for cmd in COMMANDS:
            print(f"  >> {cmd}")
            raw_buf.clear()
            data_event.clear()

            await client.write_gatt_char(NUS_TX_UUID, (cmd + "\r\n").encode())

            # Accumulate bytes until the buffer contains a complete \r\n-terminated line
            # starting with OK or ERROR.  We MUST see the terminator before accepting —
            # otherwise we'd match a truncated MTU fragment like "OK Bos" instead of
            # the full "OK Bosean FS-600;Rad Pro 2.0/en;...\r\n".
            response_line: str | None = None
            deadline = asyncio.get_event_loop().time() + RESPONSE_TIMEOUT

            while asyncio.get_event_loop().time() < deadline:
                remaining = deadline - asyncio.get_event_loop().time()
                data_event.clear()
                try:
                    await asyncio.wait_for(data_event.wait(), timeout=max(0.05, remaining))
                except asyncio.TimeoutError:
                    pass

                # Only accept a line once we see its terminator in the buffer
                raw = bytes(raw_buf)
                for terminator in (b'\r\n', b'\n', b'\r'):
                    idx = raw.find(terminator)
                    if idx == -1:
                        continue
                    candidate = raw[:idx]
                    line = ''.join(chr(b) if 32 <= b < 127 else '' for b in candidate).strip()
                    if line.startswith("OK") or line.startswith("ERROR"):
                        response_line = line
                        break
                if response_line:
                    break

            if response_line is None:
                # Print raw buffer for debug
                raw_printable = ''.join(chr(b) if 32 <= b < 127 else '·' for b in raw_buf)
                raw_hex = raw_buf.hex(' ')
                if raw_printable.strip():
                    print(f"     (raw text: {raw_printable[:80]!r})")
                    print(f"     (raw hex:  {raw_hex[:120]})")
                print("     (no response — timeout)")
                failed += 1
            elif response_line.startswith("OK"):
                print(f"     << {response_line}")
                passed += 1
            elif response_line.startswith("ERROR"):
                print(f"     << {response_line}  (not supported on this device)")
                errors += 1

        await client.stop_notify(NUS_RX_UUID)

    print()
    print("=" * 48)
    print(f"Results: {passed} OK  |  {errors} ERROR  |  {failed} no-response")
    print("=" * 48)
    return failed == 0


async def main():
    device = await find_device()
    if not device:
        print(f"\nERROR: '{DEVICE_NAME}' not found.")
        print("Check that the device is powered on and within BLE range.")
        print("Run 'make ble-scan' to list all visible BLE devices.")
        sys.exit(1)

    ok = await run_tests(device.address)
    sys.exit(0 if ok else 1)


asyncio.run(main())
