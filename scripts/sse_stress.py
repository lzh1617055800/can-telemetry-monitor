#!/usr/bin/env python3
"""Measure multi-client CAN-to-SSE delivery on one Linux host."""

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
    index = max(
        0,
        min(
            len(ordered) - 1,
            int((percentage / 100.0) * len(ordered) + 0.999999) - 1,
        ),
    )
    return ordered[index]


def receive_client(index, url, can_id, count, timeout_seconds, barrier, result):
    parsed = urlparse(url)
    connection = None

    try:
        if parsed.scheme != "http":
            raise RuntimeError("SSE benchmark currently expects http://")

        path = parsed.path or "/"
        if parsed.query:
            path += "?" + parsed.query

        connection = http.client.HTTPConnection(
            parsed.hostname,
            parsed.port or 80,
            timeout=0.5,
        )
        connection.request("GET", path)
        response = connection.getresponse()
        result["status"] = response.status
        if response.status != 200:
            raise RuntimeError(f"SSE HTTP status: {response.status}")

        # Do not send any CAN frame until every client has received headers.
        barrier.wait(timeout=5.0)
        deadline = time.monotonic() + timeout_seconds

        while len(result["sequences"]) < count:
            if time.monotonic() >= deadline:
                break

            try:
                line = response.readline()
            except (TimeoutError, socket.timeout):
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
                    max(0.0, (arrival_ns - timestamp_ns) / 1_000_000.0)
                )
            result["sequences"].append(int(frame.get("sequence", 0)))
            result["arrival_ns"].append(arrival_ns)

    except threading.BrokenBarrierError:
        result["error"] = "not all SSE clients became ready"
    except Exception as exception:  # Keep the benchmark output useful.
        result["error"] = str(exception)
        try:
            barrier.abort()
        except threading.BrokenBarrierError:
            pass
    finally:
        if connection is not None:
            connection.close()


def send_can_frames(interface_name, can_id, count, rate_hz):
    can_socket = socket.socket(socket.AF_CAN, socket.SOCK_RAW, socket.CAN_RAW)
    can_socket.bind((interface_name,))
    payload = bytearray(8)
    period = 1.0 / rate_hz if rate_hz > 0 else 0.0
    started_at = time.monotonic()
    sent = 0

    try:
        for index in range(count):
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
    return {
        "requested_frames": count,
        "sent_frames": sent,
        "requested_rate_fps": rate_hz,
        "actual_rate_fps": round(sent / elapsed, 3) if elapsed > 0 else None,
        "elapsed_seconds": round(elapsed, 3),
    }


def summarize_client(result, expected):
    sequences = result["sequences"]
    unique_sequences = set(sequences)
    duplicate_frames = len(sequences) - len(unique_sequences)
    out_of_order_frames = sum(
        1
        for previous, current in zip(sequences, sequences[1:])
        if current <= previous
    )
    sequence_gaps = 0
    if unique_sequences:
        sequence_gaps = max(
            0,
            max(unique_sequences) - min(unique_sequences) + 1
            - len(unique_sequences),
        )

    arrivals = result["arrival_ns"]
    duration = (
        (arrivals[-1] - arrivals[0]) / 1_000_000_000.0
        if len(arrivals) >= 2
        else None
    )
    received = len(sequences)
    return {
        "client": result["client"],
        "http_status": result.get("status"),
        "expected_frames": expected,
        "received_frames": received,
        "lost_frames": max(0, expected - received),
        "duplicate_frames": duplicate_frames,
        "sequence_gaps": sequence_gaps,
        "out_of_order_frames": out_of_order_frames,
        "received_fps": round((received - 1) / duration, 3)
        if duration and received >= 2
        else None,
        "average_latency_ms": round(
            sum(result["latencies_ms"]) / len(result["latencies_ms"]), 3
        )
        if result["latencies_ms"]
        else None,
        "p50_latency_ms": round(percentile(result["latencies_ms"], 50), 3)
        if result["latencies_ms"]
        else None,
        "p99_latency_ms": round(percentile(result["latencies_ms"], 99), 3)
        if result["latencies_ms"]
        else None,
        "error": result.get("error"),
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--interface", default="vcan0")
    parser.add_argument(
        "--url",
        default="http://127.0.0.1:8080/api/can/stream?after=0",
    )
    parser.add_argument("--id", type=lambda value: int(value, 0), default=0x5A0)
    parser.add_argument("--frames", type=int, default=1000)
    parser.add_argument("--rate", type=float, default=1000.0)
    parser.add_argument("--clients", type=int, default=10)
    parser.add_argument("--timeout", type=float, default=30.0)
    args = parser.parse_args()

    if args.frames <= 0 or args.rate <= 0 or args.clients <= 0:
        parser.error("frames, rate and clients must be positive")

    barrier = threading.Barrier(args.clients + 1)
    results = []
    threads = []

    for index in range(args.clients):
        result = {
            "client": index,
            "status": None,
            "sequences": [],
            "latencies_ms": [],
            "arrival_ns": [],
            "error": None,
        }
        results.append(result)
        thread = threading.Thread(
            target=receive_client,
            args=(
                index,
                args.url,
                args.id,
                args.frames,
                args.timeout,
                barrier,
                result,
            ),
            daemon=True,
        )
        threads.append(thread)
        thread.start()

    send_result = None
    setup_error = None
    try:
        barrier.wait(timeout=5.0)
        send_result = send_can_frames(
            args.interface,
            args.id,
            args.frames,
            args.rate,
        )
    except threading.BrokenBarrierError:
        setup_error = "not all SSE clients became ready"
    except Exception as exception:
        setup_error = str(exception)

    for thread in threads:
        thread.join(timeout=args.timeout + 2.0)

    clients = [summarize_client(result, args.frames) for result in results]
    all_latencies = [
        latency
        for result in results
        for latency in result["latencies_ms"]
    ]
    total_expected = args.frames * args.clients
    total_received = sum(client["received_frames"] for client in clients)
    output = {
        "interface": args.interface,
        "can_id": f"0x{args.id:X}",
        "clients": args.clients,
        "expected_total_frames": total_expected,
        "received_total_frames": total_received,
        "lost_total_frames": max(0, total_expected - total_received),
        "all_clients_complete": all(
            client["received_frames"] == args.frames for client in clients
        ),
        "aggregate_average_latency_ms": round(
            sum(all_latencies) / len(all_latencies), 3
        )
        if all_latencies
        else None,
        "aggregate_p50_latency_ms": round(percentile(all_latencies, 50), 3)
        if all_latencies
        else None,
        "aggregate_p99_latency_ms": round(percentile(all_latencies, 99), 3)
        if all_latencies
        else None,
        "sender": send_result,
        "setup_error": setup_error,
        "clients_detail": clients,
    }
    print(json.dumps(output, indent=2))

    success = (
        setup_error is None
        and send_result is not None
        and output["all_clients_complete"]
        and output["lost_total_frames"] == 0
        and all(
            client["duplicate_frames"] == 0
            and client["sequence_gaps"] == 0
            and client["out_of_order_frames"] == 0
            and client["error"] is None
            for client in clients
        )
    )
    return 0 if success else 3


if __name__ == "__main__":
    raise SystemExit(main())
