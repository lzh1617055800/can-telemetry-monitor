#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$project_dir"

"$project_dir/scripts/vcan_up.sh"
cmake -S . -B build/debug \
    -DCMAKE_BUILD_TYPE=Debug \
    -DBUILD_TESTING=ON
cmake --build build/debug -j"$(nproc)"
ctest --test-dir build/debug --output-on-failure
./build/debug/can_loopback_demo
