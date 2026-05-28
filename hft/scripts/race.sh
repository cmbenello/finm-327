#!/usr/bin/env bash
set -euo pipefail
DUR=${1:-65}
cd ~/hft_client
make -j4 >/dev/null
rm -f /tmp/results.json
pkill -f 'bin/server' 2>/dev/null || true
pkill -f 'bin/client' 2>/dev/null || true
sleep 0.3

taskset -c 30 ./bin/server >/tmp/server.log 2>&1 &
SERVER_PID=$!
sleep 0.5

sudo -n taskset -c 4  ./bin/client       127.0.0.1 12345 cascade-fast  >/tmp/c_fast.log  2>&1 &
F_PID=$!
sudo -n taskset -c 12 ./bin/client_naive 127.0.0.1 12345 cascade-naive >/tmp/c_naive.log 2>&1 &
N_PID=$!

sleep "$DUR"

kill "$F_PID" "$N_PID" "$SERVER_PID" 2>/dev/null || true
sleep 0.4
pkill -f 'bin/server' 2>/dev/null || true
pkill -f 'bin/client' 2>/dev/null || true

if [ -f /tmp/results.json ]; then
  python3 - <<'PY'
import json, statistics as st
with open('/tmp/results.json') as f: data = json.load(f)
by={}; wins={}
for ch in data:
    for p in ch.get('players',[]): by.setdefault(p['name'],[]).append(p['latency_ms'])
    w=ch.get('winner','')
    if w: wins[w]=wins.get(w,0)+1
print(f"challenges: {len(data)}")
for n in sorted(by):
    L=by[n]
    print(f"  {n:14s} n={len(L):3d} wins={wins.get(n,0):3d} "
          f"avg={st.mean(L):.2f}ms median={st.median(L):.2f}ms min={min(L)}ms max={max(L)}ms")
PY
fi
