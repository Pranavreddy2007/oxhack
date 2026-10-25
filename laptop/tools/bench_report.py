"""Latency tables from raw benchmark CSV files (label,stage,ns).

Every number comes from the raw samples. Nothing is typed by hand.

Usage:
    python -m laptop.tools.bench_report --raw results/raw/bench/*_raw.csv --out results/bench_laptop.md
"""

from __future__ import annotations

import argparse
import csv
import json
import math
from collections import defaultdict
from pathlib import Path

import numpy as np


def stats(ns):
    v = np.sort(np.asarray(ns, dtype=float))
    n = len(v)

    def q(p):  # nearest-rank, the same rule as metrics.hpp
        r = max(1, math.ceil(p * n))
        return v[min(n, r) - 1]

    return {
        "count": n,
        "mean": float(v.mean()),
        "p50": float(q(0.50)),
        "p95": float(q(0.95)),
        "p99": float(q(0.99)),
        "p999": float(q(0.999)) if n >= 10000 else None,
        "max": float(v[-1]),
    }


def load(paths):
    data = defaultdict(lambda: defaultdict(list))
    for p in paths:
        with open(p, newline="") as f:
            for r in csv.DictReader(f):
                data[r["label"]][r["stage"]].append(int(r["ns"]))
    return data


def table(data, metas):
    lines = ["| Run | Stage | Count | Mean ms | p50 ms | p95 ms | p99 ms | p99.9 ms | Max ms |",
             "|---|---|---|---|---|---|---|---|---|"]
    for label in sorted(data):
        for stage in sorted(data[label]):
            s = stats(data[label][stage])
            p999 = "n/a (count < 10000)" if s["p999"] is None else f"{s['p999'] / 1e6:.3f}"
            lines.append(f"| {label} | {stage} | {s['count']} | {s['mean'] / 1e6:.3f} | {s['p50'] / 1e6:.3f} | "
                         f"{s['p95'] / 1e6:.3f} | {s['p99'] / 1e6:.3f} | {p999} | {s['max'] / 1e6:.3f} |")
    if metas:
        lines += ["", "| Run | Backend | Threads req. | HW threads | N | K | H | Wall s | CPU util (cores) | Peak RSS kB | Rollout member-ticks/s | Rollout cell-updates/s |",
                  "|---|---|---|---|---|---|---|---|---|---|---|---|"]
        for m in metas:
            lines.append(f"| {m['label']} | {m['backend']} | {m['threads_requested']} | {m['hardware_threads']} | {m['N']} | "
                         f"{m['K']} | {m['H']} | {m['wall_s']:.2f} | {m['cpu_util_cores']:.2f} | {m['vm_hwm_kb']} | "
                         f"{m['rollout_member_ticks_per_s']:.3g} | {m['rollout_cell_updates_per_s']:.3g} |")
    return "\n".join(lines) + "\n"


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--raw", nargs="+", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--title", default="EmberCast benchmark tables")
    args = ap.parse_args(argv)
    data = load(args.raw)
    metas = []
    for p in args.raw:
        if not p.endswith("_raw.csv"):
            continue
        mp = Path(p[: -len("_raw.csv")] + "_meta.json")
        if mp.exists():
            metas.append(json.loads(mp.read_text()))
    text = f"# {args.title}\n\nMade by `laptop/tools/bench_report.py` from: " + ", ".join(
        f"`{p}`" for p in args.raw) + "\n\n" + table(data, metas)
    Path(args.out).parent.mkdir(parents=True, exist_ok=True)
    Path(args.out).write_text(text)
    print(text)


if __name__ == "__main__":
    main()
