#!/usr/bin/env python3
"""Inject a controlled stream of CAN frames into a Linux CAN interface.

This is deliberately separate from the SSE benchmark.  REST pressure tests
need a CAN producer without opening an SSE connection, otherwise the test
would mix two different measurements.
"""

import argparse
import json
import socket
import struct
import time


CAN_EFF_FLAG = 0x80000000
CAN_EFF_MASK = 0x1FFFFFFF
CAN_SFF_MASK = 0x7FF


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--interface", default="vcan0")
    parser.add_argument("--id", type=lambda value: int(value, 0), default=0x5A0)
    parser.add_argument("--frames", type=int, default=60000)
    parser.add_argument("--rate", type=float, default=2000.0)
    parser.add_argument("--extended", action="store_true")
    args = parser.parse_args()

    if args.frames <= 0:
        parser.error("frames must be positive")
    if args.rate <= 0:
        parser.error("rate must be positive")

    maximum_id = CAN_EFF_MASK if args.extended else CAN_SFF_MASK
    if args.id < 0 or args.id > maximum_id:
        parser.error("CAN ID is outside the selected frame type range")

    can_id = args.id | CAN_EFF_FLAG if args.extended else args.id
    payload = bytearray(8)
    period = 1.0 / args.rate
    started_at = time.monotonic()
    sent = 0

    can_socket = socket.socket(socket.AF_CAN, socket.SOCK_RAW, socket.CAN_RAW)
    can_socket.bind((args.interface,))

    try:
        for index in range(args.frames):
            payload[0] = 0xCA
            payload[1] = index & 0xFF
            payload[2] = (index >> 8) & 0xFF
            payload[3] = (index >> 16) & 0xFF
            payload[4:] = b"\x11\x22\x33\x44"

            can_socket.send(struct.pack("=IB3x8s", can_id, 8, payload))
            sent += 1

            target = started_at + sent * period
            remaining = target - time.monotonic()
            if remaining > 0:
                time.sleep(remaining)
    finally:
        can_socket.close()

    elapsed = time.monotonic() - started_at
    result = {
        "interface": args.interface,
        "can_id": f"0x{args.id:X}",
        "requested_frames": args.frames,
        "sent_frames": sent,
        "requested_rate_fps": args.rate,
        "actual_rate_fps": round(sent / elapsed, 3) if elapsed > 0 else None,
        "elapsed_seconds": round(elapsed, 3),
    }
    print(json.dumps(result, indent=2))
    return 0 if sent == args.frames else 1


if __name__ == "__main__":
    raise SystemExit(main())
