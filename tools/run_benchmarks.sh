#!/usr/bin/env bash
# Build in release mode and run both benchmarks three times.
set -euo pipefail
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo >/dev/null
cmake --build build >/dev/null
for i in 1 2 3; do
  echo "== run $i"
  ./build/signal_pipeline bench fft --workers 1,2,4,8 --windows 40000
done
./build/signal_pipeline bench batching --samples 8388608 --consumers 4
