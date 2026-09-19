#!/usr/bin/env bash
set -euo pipefail

sudo modprobe vcan

if ! ip link show vcan0 >/dev/null 2>&1; then
    sudo ip link add dev vcan0 type vcan
fi

sudo ip link set dev vcan0 up
ip -details link show vcan0
