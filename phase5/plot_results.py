#!/usr/bin/env python3
"""plot phase5 benchmark output as a 2-panel chart."""
import csv
import os
import sys

import matplotlib.pyplot as plt

CSV_PATH = sys.argv[1] if len(sys.argv) > 1 else "bench_results.csv"
OUT_PATH = sys.argv[2] if len(sys.argv) > 2 else "perf_chart.png"

def load(path):
    rows = []
    with open(path) as f:
        for r in csv.DictReader(f):
            rows.append({
                "size": int(r["size"]),
                "book": r["book"],
                "workload": r["workload"],
                "seconds": float(r["seconds"]),
                "ops_per_sec": float(r["ops_per_sec"]),
            })
    return rows

def series(rows, book, workload, key):
    pts = sorted([(r["size"], r[key]) for r in rows
                  if r["book"] == book and r["workload"] == workload])
    return [p[0] for p in pts], [p[1] for p in pts]

def main():
    if not os.path.exists(CSV_PATH):
        print(f"missing {CSV_PATH}; run ./bench_app first", file=sys.stderr)
        sys.exit(1)
    rows = load(CSV_PATH)

    fig, axes = plt.subplots(1, 2, figsize=(13, 5))

    # left: execution time vs size, log-log
    ax = axes[0]
    for book, marker in [("baseline", "o"), ("optimized", "s")]:
        for workload, ls in [("add", "-"), ("mixed", "--")]:
            x, y = series(rows, book, workload, "seconds")
            ax.plot(x, y, marker=marker, linestyle=ls, label=f"{book} / {workload}")
    ax.set_xscale("log"); ax.set_yscale("log")
    ax.set_xlabel("number of orders")
    ax.set_ylabel("execution time (s)")
    ax.set_title("HFT Order Book — execution time")
    ax.grid(True, which="both", alpha=0.3)
    ax.legend(loc="upper left", fontsize=9)

    # right: throughput vs size
    ax = axes[1]
    for book, marker in [("baseline", "o"), ("optimized", "s")]:
        for workload, ls in [("add", "-"), ("mixed", "--")]:
            x, y = series(rows, book, workload, "ops_per_sec")
            ax.plot(x, y, marker=marker, linestyle=ls, label=f"{book} / {workload}")
    ax.set_xscale("log")
    ax.set_xlabel("number of orders")
    ax.set_ylabel("throughput (ops / sec)")
    ax.set_title("HFT Order Book — throughput")
    ax.grid(True, which="both", alpha=0.3)
    ax.legend(loc="lower left", fontsize=9)

    fig.suptitle("phase5: baseline vs optimized order book (string IDs)")
    fig.tight_layout()
    fig.savefig(OUT_PATH, dpi=140)
    print(f"wrote {OUT_PATH}")

if __name__ == "__main__":
    main()
