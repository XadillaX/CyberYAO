#!/usr/bin/env python3
"""通过 BLE Current Time Service 将电脑本地时间同步到 CyberYAO。"""

from __future__ import annotations

import argparse
import asyncio
from datetime import datetime

from bleak import BleakClient, BleakScanner


DEVICE_NAME = "CyberYAO-Time"
CURRENT_TIME_UUID = "00002a2b-0000-1000-8000-00805f9b34fb"


async def sync(timeout: float) -> None:
    device = await BleakScanner.find_device_by_name(DEVICE_NAME, timeout=timeout)
    if device is None:
        raise RuntimeError(f"未找到蓝牙设备 {DEVICE_NAME}")

    now = datetime.now().astimezone()
    payload = (
        now.year.to_bytes(2, "little")
        + bytes(
            (
                now.month,
                now.day,
                now.hour,
                now.minute,
                now.second,
                now.isoweekday(),
                0,
                1,
            )
        )
    )
    async with BleakClient(device) as client:
        await client.write_gatt_char(CURRENT_TIME_UUID, payload, response=True)
    print(f"已同步 {now:%Y-%m-%d %H:%M:%S %z} 到 {DEVICE_NAME}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--timeout", type=float, default=15.0)
    args = parser.parse_args()
    asyncio.run(sync(args.timeout))


if __name__ == "__main__":
    main()
