#!/usr/bin/env python3
"""Measure CAN-to-SSE delivery on one Linux host.

The CAN receiver timestamps a frame with CLOCK_MONOTONIC.  This script uses
the same clock family through time.monotonic_ns() when the SSE JSON arrives,
so the reported latency includes the receiver, event loop and TCP write path.
"""

import argparse
import http.client
import json
import socket
import struct
import threading
import time
from urllib.parse import urlparse


def percentile(values, percentage):
    if not values:
        return None
    ordered = sorted(values)
    index = max(0, min(len(ordered) - 1,
                       int((percentage / 100.0) * len(ordered) + 0.999999) - 1))
    return ordered[index]


def send_can_frames(interface_name, can_id, count, rate_hz, started, finished):
    can_socket = socket.socket(socket.AF_CAN, socket.SOCK_RAW, socket.CAN_RAW)
    can_socket.bind((interface_name,))
    payload = bytearray(8)
    period = 1.0 / rate_hz if rate_hz > 0 else 0.0

    try:
        started.set()
        start_time = time.monotonic()
        for index in range(count):
            payload[0] = 0xCA
            payload[1] = index & 0xFF
            payload[2] = (index >> 8) & 0xFF
            payload[3] = (index >> 16) & 0xFF
            payload[4] = 0x11
            payload[5] = 0x22
            payload[6] = 0x33
            payload[7] = 0x44

            # struct can_frame on Linux: can_id, can_dlc, padding, data[8].
            can_socket.send(struct.pack("=IB3x8s", can_id, 8, payload))

            if period > 0 and index + 1 < count:
                target = start_time + (index + 1) * period
                remaining = target - time.monotonic()
                if remaining > 0:
                    time.sleep(remaining)
    finally:
        can_socket.close()
        finished.set()


def receive_sse(url, interface_name, can_id, count, timeout_seconds, result):
    parsed = urlparse(url)
    if parsed.scheme != "http":
        result["error"] = "SSE benchmark currently expects an http:// URL"
        result["ready"].set()
        return

    path = parsed.path or "/"
    if parsed.query:
        path += "?" + parsed.query

    connection = http.client.HTTPConnection(
        parsed.hostname,
        parsed.port or 80,
        timeout=0.5)

    try:
        connection.request("GET", path)
        response = connection.getresponse()
        if response.status != 200:
            result["error"] = f"SSE HTTP status: {response.status}"
            result["ready"].set()
            return

        result["ready"].set()
        deadline = time.monotonic() + timeout_seconds

        while len(result["latencies_ms"]) < count:
            if time.monotonic() >= deadline:
                break

            try:
                line = response.readline()
            except TimeoutError:
                continue

            if not line:
                break

            text = line.decode("utf-8", errors="replace").strip()
            if not text.startswith("data: "):
                continue

            try:
                frame = json.loads(text[6:])
            except json.JSONDecodeError:
                continue

            if int(frame.get("id", -1)) != can_id:
                continue

            timestamp_ns = int(frame.get("timestamp_ns", 0))
            arrival_ns = time.monotonic_ns()
            if timestamp_ns > 0:
                result["latencies_ms"].append(
                    max(0.0, (arrival_ns - timestamp_ns) / 1_000_000.0))
            result["sequences"].append(int(frame.get("sequence", 0)))
            result["arrival_ns"].append(arrival_ns)
    except Exception as exception:  # Keep the benchmark output useful.
        result["error"] = str(exception)
    finally:
        result["ready"].set()
        connection.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--interface", default="vcan0")
    parser.add_argument("--url",
                        default="http://127.0.0.1:8080/api/can/stream?after=0")
    parser.add_argument("--id", type=lambda value: int(value, 0), default=0x5A0)
    parser.add_argument("--frames", type=int, default=1000)
    parser.add_argument("--rate", type=float, default=100.0,
                        help="CAN frames per second")
    parser.add_argument("--timeout", type=float, default=30.0)
    args = parser.parse_args()

    if args.frames <= 0 or args.rate < 0:
        parser.error("frames must be positive and rate cannot be negative")

    result = {
        "ready": threading.Event(),
        "latencies_ms": [],
        "sequences": [],
        "arrival_ns": [],
        "error": None,
    }
    receiver = threading.Thread(
        target=receive_sse,
        args=(args.url, args.interface, args.id, args.frames,
              args.timeout, result),
        daemon=True)
    receiver.start()

    if not result["ready"].wait(5.0):
        result["error"] = "SSE endpoint did not become ready within 5 seconds"
    elif result["error"] is None:
        sender_started = threading.Event()
        sender_finished = threading.Event()
        sender = threading.Thread(
            target=send_can_frames,
            args=(args.interface, args.id, args.frames, args.rate,
                  sender_started, sender_finished),
            daemon=True)
        sender.start()
        sender_finished.wait(args.timeout)
        sender.join(timeout=1.0)

    receiver.join(timeout=args.timeout + 2.0)

    received = len(result["sequences"])
    latencies = result["latencies_ms"]
    arrival_times = result["arrival_ns"]
    delivery_duration_s = (
        (arrival_times[-1] - arrival_times[0]) / 1_000_000_000.0
        if len(arrival_times) >= 2 else None)
    unique_sequences = set(result["sequences"])
    duplicate_frames = received - len(unique_sequences)
    sequence_gaps = 0
    if unique_sequences:
        sequence_gaps = max(
            0,
            max(unique_sequences) - min(unique_sequences) + 1
            - len(unique_sequences),
        )
    out_of_order_frames = sum(
        1
        for previous, current in zip(
            result["sequences"], result["sequences"][1:])
        if current <= previous
    )
    output = {
        "interface": args.interface,
        "can_id": f"0x{args.id:X}",
        "expected_frames": args.frames,
        "received_frames": received,
        "lost_frames": max(0, args.frames - received),
        "duplicate_frames": duplicate_frames,
        "sequence_gaps": sequence_gaps,
        "out_of_order_frames": out_of_order_frames,
        "received_fps": round((received - 1) / delivery_duration_s, 3)
            if delivery_duration_s and delivery_duration_s > 0
            and received >= 2 else None,
        "average_latency_ms":
            round(sum(latencies) / len(latencies), 3) if latencies else None,
        "p50_latency_ms":
            round(percentile(latencies, 50), 3) if latencies else None,
        "p99_latency_ms":
            round(percentile(latencies, 99), 3) if latencies else None,
        "error": result["error"],
    }
    print(json.dumps(output, indent=2))
    if result["error"] is not None:
        return 2
    return 0 if (
        received == args.frames
        and duplicate_frames == 0
        and sequence_gaps == 0
        and out_of_order_frames == 0
    ) else 3


if __name__ == "__main__":
    raise SystemExit(main())
