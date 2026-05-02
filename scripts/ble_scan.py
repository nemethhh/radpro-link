#!/usr/bin/env python3
"""Scan for nearby BLE devices, highlighting any RadPro-Link devices."""

import asyncio
from bleak import BleakScanner

NUS_SERVICE = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
DEVICE_NAME = "RadPro-Link"


async def main():
    print("Scanning for BLE devices (10 seconds)...")
    discovered = await BleakScanner.discover(timeout=10.0, return_adv=True)

    radpro = []
    others = []

    for addr, (device, adv) in discovered.items():
        name = device.name or ""
        uuids = [u.lower() for u in (adv.service_uuids if adv else [])]
        rssi = adv.rssi if adv else None
        entry = (rssi or -999, addr, name, uuids)
        if DEVICE_NAME.lower() in name.lower() or NUS_SERVICE in uuids:
            radpro.append(entry)
        else:
            others.append(entry)

    if radpro:
        print(f"\n{'='*50}")
        print("RadPro-Link device(s) found:")
        for rssi, addr, name, uuids in sorted(radpro, reverse=True):
            print(f"  >>> {addr}  RSSI={rssi:4}  name={name!r}")
        print(f"{'='*50}")
    else:
        print(f"\nNo '{DEVICE_NAME}' found — device may not be advertising.")

    print(f"\nAll devices ({len(discovered)} total):")
    for rssi, addr, name, uuids in sorted(others, key=lambda x: -x[0]):
        rssi_s = f"{rssi:4}" if rssi is not None else "  ? "
        print(f"  {addr}  RSSI={rssi_s}  {name or '(no name)'}")


asyncio.run(main())
