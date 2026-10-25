"""Summary of the TUNE parameter sweep (results/raw/tune/*.csv).

Usage: python -m laptop.tools.tune_summary --dir results/raw/tune --out results/tune_summary.md
"""

from __future__ import annotations

import argparse
import csv
import glob
from pathlib import Path

import numpy as np

from laptop.tools.evaluate import cvar


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", default="results/raw/tune")
    ap.add_argument("--out", default="results/tune_summary.md")
    args = ap.parse_args(argv)
    base = {}
    for r in csv.DictReader(open(Path(args.dir) / "base.csv")):
        base.setdefault(r["policy"], {})[int(r["seed"])] = float(r["loss"])
    lines = ["# TUNE sweep (TUNE seeds only)", "",
             "Made by `laptop/tools/tune_summary.py`. TEST seeds were not used.", "",
             "| Baseline | Mean loss |", "|---|---|"]
    for p in ("deterministic", "front_nearest", "oracle"):
        lines.append(f"| {p} | {np.mean(list(base[p].values())):.2f} |")
    lines += ["", "| Run (EmberCast) | Seeds | Mean loss | CVaR95 loss | Mean diff vs B1 | Win rate vs B1 | Brier |",
              "|---|---|---|---|---|---|---|"]
    for f in sorted(glob.glob(str(Path(args.dir) / "*.csv"))):
        if f.endswith("base.csv"):
            continue
        rows = list(csv.DictReader(open(f)))
        loss = np.array([float(r["loss"]) for r in rows])
        b1 = np.array([base["deterministic"][int(r["seed"])] for r in rows])
        n = sum(int(r["pred_n"]) for r in rows)
        brier = sum(float(r["brier"]) for r in rows) / n
        lines.append(f"| {rows[0]['tag']} | {len(rows)} | {loss.mean():.2f} | {cvar(loss):.2f} | "
                     f"{np.mean(loss - b1):.2f} | {np.mean(loss < b1):.2f} | {brier:.4f} |")
    Path(args.out).write_text("\n".join(lines) + "\n")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
