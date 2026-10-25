"""Evaluation report from embercast_replay CSV files.

Every number in the report comes from the CSV rows. Nothing is typed by hand.

Usage:
    python -m laptop.tools.evaluate --csv results/raw/test_main.csv [more.csv ...] \
        --out results
"""

from __future__ import annotations

import argparse
import csv
import json
import math
from collections import defaultdict
from pathlib import Path

import numpy as np

BOOT = 10000
BOOT_SEED = 12345


def load(paths):
    rows = []
    for p in paths:
        with open(p, newline="") as f:
            rows.extend(csv.DictReader(f))
    return rows


def cvar(x, alpha=0.95):
    """Mean of the worst (1 - alpha) fraction (at least one value)."""
    x = np.sort(np.asarray(x, dtype=float))[::-1]
    k = max(1, int(math.ceil((1 - alpha) * len(x) - 1e-9)))
    return float(x[:k].mean())


def paired_bootstrap(a, b, stat=np.mean, n=BOOT, seed=BOOT_SEED):
    """95% CI of stat(a) - stat(b) with paired resampling over seeds."""
    a = np.asarray(a, dtype=float)
    b = np.asarray(b, dtype=float)
    rng = np.random.default_rng(seed)
    idx = rng.integers(0, len(a), size=(n, len(a)))
    if stat is np.mean:
        d = (a[idx] - b[idx]).mean(axis=1)
    else:
        d = np.array([stat(a[i]) - stat(b[i]) for i in idx])
    lo, hi = np.percentile(d, [2.5, 97.5])
    return float(stat(a) - stat(b)), float(lo), float(hi)


def pred_metrics(rows):
    n = sum(int(r["pred_n"]) for r in rows)
    if n == 0:
        return None
    brier = sum(float(r["brier"]) for r in rows) / n
    logloss = sum(float(r["logloss"]) for r in rows) / n
    bins = []
    ece = 0.0
    for b in range(10):
        bn = sum(int(r[f"bin_n{b}"]) for r in rows)
        bp = sum(float(r[f"bin_p{b}"]) for r in rows)
        by = sum(float(r[f"bin_y{b}"]) for r in rows)
        if bn:
            bins.append({"bin": b, "n": bn, "mean_p": bp / bn, "freq": by / bn})
            ece += bn / n * abs(bp / bn - by / bn)
    cn = sum(int(r["crps_n"]) for r in rows)
    crps = sum(float(r["crps"]) for r in rows) / cn if cn else float("nan")
    var = sum(float(r["var_sum"]) for r in rows) / cn if cn else float("nan")
    mse = sum(float(r["sqerr_sum"]) for r in rows) / cn if cn else float("nan")
    ln = sum(int(r["lost_n"]) for r in rows)
    lost = sum(float(r["lost_brier"]) for r in rows) / ln if ln else float("nan")
    return {
        "cells": n,
        "brier": brier,
        "logloss": logloss,
        "ece": ece,
        "crps_area": crps,
        "spread_skill": math.sqrt(var) / math.sqrt(mse) if mse > 0 else float("nan"),
        "brier_cluster_lost": lost,
        "bins": bins,
    }


def summarise(rows):
    """Group rows by (tag, stress) and compute decision and prediction metrics."""
    groups = defaultdict(lambda: defaultdict(dict))
    for r in rows:
        groups[(r["tag"], r["stress"])][r["policy"]][int(r["seed"])] = r
    out = []
    for (tag, stress), pols in sorted(groups.items()):
        entry = {"tag": tag, "stress": stress, "policies": {}, "paired": {}}
        for pol, by_seed in pols.items():
            losses = [float(by_seed[s]["loss"]) for s in sorted(by_seed)]
            entry["policies"][pol] = {
                "seeds": len(losses),
                "mean_loss": float(np.mean(losses)),
                "cvar95_loss": cvar(losses),
                "K": int(next(iter(by_seed.values()))["K"]),
                "pred": pred_metrics(list(by_seed.values())),
            }
        if "ensemble" in pols:
            ens = pols["ensemble"]
            for base in ("deterministic", "front_nearest", "oracle"):
                if base not in pols:
                    continue
                seeds = sorted(set(ens) & set(pols[base]))
                a = [float(ens[s]["loss"]) for s in seeds]
                b = [float(pols[base][s]["loss"]) for s in seeds]
                d, lo, hi = paired_bootstrap(a, b)
                dc, clo, chi = paired_bootstrap(a, b, stat=cvar, n=2000)
                entry["paired"][base] = {
                    "seeds": len(seeds),
                    "mean_diff": d,
                    "ci95": [lo, hi],
                    "cvar95_diff": dc,
                    "cvar95_ci95": [clo, chi],
                    "win_rate": float(np.mean([x < y for x, y in zip(a, b)])),
                    "tie_rate": float(np.mean([x == y for x, y in zip(a, b)])),
                }
            if "oracle" in pols:
                for pol in pols:
                    seeds = sorted(set(pols[pol]) & set(pols["oracle"]))
                    reg = [float(pols[pol][s]["loss"]) - float(pols["oracle"][s]["loss"]) for s in seeds]
                    entry["policies"][pol]["regret_vs_oracle"] = float(np.mean(reg))
        out.append(entry)
    return out


def fmt(x, nd=2):
    return "n/a" if x is None or (isinstance(x, float) and math.isnan(x)) else f"{x:.{nd}f}"


def write_report(summary, out_dir: Path, source_files):
    out_dir.mkdir(parents=True, exist_ok=True)
    lines = ["# EmberCast Evaluation Report", "",
             "Simulation only. Not for operational use.", "",
             "Made by `laptop/tools/evaluate.py` from: " + ", ".join(f"`{s}`" for s in source_files),
             "", "Losses are realised asset loss in the truth simulator over the episode.",
             "Paired differences are EmberCast minus baseline (negative = EmberCast is better).",
             f"95% CIs: paired bootstrap over seeds, {BOOT} resamples (CVaR: 2000), seed {BOOT_SEED}.", ""]
    for e in summary:
        lines += [f"## Tag `{e['tag']}`, stress `{e['stress']}`", "",
                  "| Policy | K | Seeds | Mean loss | CVaR95 loss | Regret vs oracle | Brier | Log loss | ECE | CRPS area | Spread/skill |",
                  "|---|---|---|---|---|---|---|---|---|---|---|"]
        for pol in ("ensemble", "deterministic", "front_nearest", "oracle"):
            if pol not in e["policies"]:
                continue
            p = e["policies"][pol]
            pm = p["pred"] or {}
            lines.append(
                f"| {pol} | {p['K']} | {p['seeds']} | {fmt(p['mean_loss'])} | {fmt(p['cvar95_loss'])} | "
                f"{fmt(p.get('regret_vs_oracle'))} | {fmt(pm.get('brier'), 4)} | {fmt(pm.get('logloss'), 4)} | "
                f"{fmt(pm.get('ece'), 4)} | {fmt(pm.get('crps_area'), 1)} | {fmt(pm.get('spread_skill'), 2)} |")
        if e["paired"]:
            lines += ["", "| EmberCast vs | Mean diff | 95% CI | CI excludes 0 | CVaR95 diff | CVaR95 95% CI | Win rate | Tie rate |",
                      "|---|---|---|---|---|---|---|---|"]
            for base, p in e["paired"].items():
                excl = "yes" if (p["ci95"][1] < 0 or p["ci95"][0] > 0) else "no"
                lines.append(
                    f"| {base} | {fmt(p['mean_diff'])} | [{fmt(p['ci95'][0])}, {fmt(p['ci95'][1])}] | {excl} | "
                    f"{fmt(p['cvar95_diff'])} | [{fmt(p['cvar95_ci95'][0])}, {fmt(p['cvar95_ci95'][1])}] | "
                    f"{fmt(p['win_rate'])} | {fmt(p['tie_rate'])} |")
        lines.append("")
    (out_dir / "report.md").write_text("\n".join(lines) + "\n")
    (out_dir / "summary.json").write_text(json.dumps(summary, indent=1) + "\n")


def plots(summary, out_dir: Path):
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    main = [e for e in summary if e["tag"] == "main"] or [e for e in summary if e["stress"] == "none"]
    if main:
        e = main[0]
        fig, ax = plt.subplots(figsize=(5, 5))
        ax.plot([0, 1], [0, 1], color="#888", lw=1, ls="--", label="perfect")
        for pol, color in (("ensemble", "#c2410c"), ("deterministic", "#2563eb")):
            pm = e["policies"].get(pol, {}).get("pred")
            if not pm:
                continue
            xs = [b["mean_p"] for b in pm["bins"]]
            ys = [b["freq"] for b in pm["bins"]]
            ax.plot(xs, ys, marker="o", color=color,
                    label=f"{'EmberCast' if pol == 'ensemble' else 'B1'} (ECE {pm['ece']:.3f})")
        ax.set_xlabel("Forecast P(burn within H)")
        ax.set_ylabel("Observed frequency")
        ax.set_title(f"Reliability, tag {e['tag']}")
        ax.legend()
        fig.tight_layout()
        fig.savefig(out_dir / "reliability.png", dpi=120)
        plt.close(fig)

    # Paired differences with CI, per stress scenario.
    def name(e):
        if e["tag"] == "main":
            return "main (no stress)"
        return "stress " + e["stress"] if e["tag"] == "stress" else e["tag"]

    rows = [(name(e), base, p)
            for e in summary for base, p in e["paired"].items() if base != "oracle"]
    if rows:
        fig, ax = plt.subplots(figsize=(7, 0.45 * len(rows) + 1.2))
        labels = []
        for i, (name, base, p) in enumerate(rows):
            lo, hi = p["ci95"]
            ax.errorbar(p["mean_diff"], i, xerr=[[p["mean_diff"] - lo], [hi - p["mean_diff"]]],
                        fmt="o", color="#c2410c" if hi < 0 else "#555", capsize=3)
            labels.append(f"{name} vs {'B1' if base == 'deterministic' else 'B0'}")
        ax.axvline(0, color="#888", lw=1)
        ax.set_yticks(range(len(rows)), labels)
        ax.set_xlabel("Mean loss difference, EmberCast - baseline (95% CI)")
        fig.tight_layout()
        fig.savefig(out_dir / "paired_diff.png", dpi=120)
        plt.close(fig)


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--csv", nargs="+", required=True)
    ap.add_argument("--out", default="results")
    args = ap.parse_args(argv)
    rows = load(args.csv)
    summary = summarise(rows)
    out = Path(args.out)
    write_report(summary, out, args.csv)
    plots(summary, out)
    print((out / "report.md").read_text())


if __name__ == "__main__":
    main()
