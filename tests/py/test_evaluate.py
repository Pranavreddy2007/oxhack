import numpy as np
from hypothesis import given, settings
from hypothesis import strategies as st

from laptop.tools import evaluate


def test_cvar_takes_the_worst_tail():
    x = list(range(100))
    assert evaluate.cvar(x, 0.95) == np.mean(range(95, 100))
    assert evaluate.cvar([3.0], 0.95) == 3.0


def test_paired_bootstrap_is_deterministic_and_brackets_the_mean():
    rng = np.random.default_rng(0)
    a = rng.normal(10, 2, 60)
    b = a + 1.0 + rng.normal(0, 0.1, 60)
    d1 = evaluate.paired_bootstrap(a, b)
    d2 = evaluate.paired_bootstrap(a, b)
    assert d1 == d2
    d, lo, hi = d1
    assert lo < d < hi
    assert hi < 0


@settings(max_examples=50, deadline=None)
@given(st.lists(st.floats(0, 1e4), min_size=1, max_size=50))
def test_cvar_is_at_least_the_mean(xs):
    assert evaluate.cvar(xs) >= np.mean(xs) - 1e-6


def _row(policy, seed, loss, **kw):
    r = {"tag": "t", "stress": "none", "policy": policy, "seed": str(seed), "loss": str(loss), "K": "1",
         "pred_n": "0", "brier": "0", "logloss": "0", "crps_n": "0", "crps": "0", "var_sum": "0",
         "sqerr_sum": "0", "lost_n": "0", "lost_brier": "0"}
    for b in range(10):
        r[f"bin_p{b}"] = "0"
        r[f"bin_y{b}"] = "0"
        r[f"bin_n{b}"] = "0"
    r.update({k: str(v) for k, v in kw.items()})
    return r


def test_ece_and_summary():
    rows = []
    for s in range(5):
        rows.append(_row("ensemble", s, 10 + s, pred_n=10, brier=1.0, logloss=2.0, bin_p0=0.5, bin_y0=0, bin_n0=5,
                         bin_p9=4.75, bin_y9=5, bin_n9=5))
        rows.append(_row("deterministic", s, 12 + s))
    summary = evaluate.summarise(rows)
    e = summary[0]
    pm = e["policies"]["ensemble"]["pred"]
    # Bin 0: mean p 0.1, freq 0. Bin 9: mean p 0.95, freq 1. ECE = 0.5*0.1 + 0.5*0.05.
    assert abs(pm["ece"] - 0.075) < 1e-9
    assert e["paired"]["deterministic"]["mean_diff"] == -2.0
    assert e["paired"]["deterministic"]["win_rate"] == 1.0
