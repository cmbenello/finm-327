#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "$0")/.." && pwd)"
rsync -avz --delete \
  --exclude bin/ --exclude results/ --exclude '*.o' --exclude '.DS_Store' \
  "$HERE/" cascade:~/hft_client/
