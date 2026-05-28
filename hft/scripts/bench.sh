#!/usr/bin/env bash
set -euo pipefail
ITERS=${1:-1000}
cd ~/hft_client
make -j4 >/dev/null
./bin/micro "$ITERS"
