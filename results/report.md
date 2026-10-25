# EmberCast Evaluation Report

Simulation only. Not for operational use.

Made by `laptop/tools/evaluate.py` from: `results/raw/test/main.csv`, `results/raw/test/stress_dropout30.csv`, `results/raw/test/stress_late_scans.csv`, `results/raw/test/stress_regime.csv`, `results/raw/test/stress_stuck.csv`, `results/raw/test/abl_K128.csv`, `results/raw/test/abl_K256.csv`, `results/raw/test/abl_K32.csv`, `results/raw/test/abl_lambda0.csv`, `results/raw/test/abl_nofilter.csv`

Losses are realised asset loss in the truth simulator over the episode.
Paired differences are EmberCast minus baseline (negative = EmberCast is better).
95% CIs: paired bootstrap over seeds, 10000 resamples (CVaR: 2000), seed 12345.

## Tag `ablation_K128`, stress `none`

| Policy | K | Seeds | Mean loss | CVaR95 loss | Regret vs oracle | Brier | Log loss | ECE | CRPS area | Spread/skill |
|---|---|---|---|---|---|---|---|---|---|---|
| ensemble | 128 | 100 | 162.53 | 526.10 | n/a | 0.0733 | 0.2398 | 0.0203 | 76.4 | 1.21 |
| deterministic | 1 | 100 | 185.93 | 540.49 | n/a | 0.2420 | 2.2287 | 0.2420 | 505.1 | 0.00 |

| EmberCast vs | Mean diff | 95% CI | CI excludes 0 | CVaR95 diff | CVaR95 95% CI | Win rate | Tie rate |
|---|---|---|---|---|---|---|---|
| deterministic | -23.40 | [-33.13, -14.09] | yes | -14.39 | [-58.36, 25.10] | 0.58 | 0.23 |

## Tag `ablation_K256`, stress `none`

| Policy | K | Seeds | Mean loss | CVaR95 loss | Regret vs oracle | Brier | Log loss | ECE | CRPS area | Spread/skill |
|---|---|---|---|---|---|---|---|---|---|---|
| ensemble | 256 | 100 | 160.89 | 504.01 | n/a | 0.0711 | 0.2296 | 0.0208 | 72.9 | 1.27 |
| deterministic | 1 | 100 | 185.93 | 540.49 | n/a | 0.2420 | 2.2287 | 0.2420 | 505.1 | 0.00 |

| EmberCast vs | Mean diff | 95% CI | CI excludes 0 | CVaR95 diff | CVaR95 95% CI | Win rate | Tie rate |
|---|---|---|---|---|---|---|---|
| deterministic | -25.04 | [-35.30, -15.19] | yes | -36.48 | [-77.94, 15.46] | 0.58 | 0.22 |

## Tag `ablation_K32`, stress `none`

| Policy | K | Seeds | Mean loss | CVaR95 loss | Regret vs oracle | Brier | Log loss | ECE | CRPS area | Spread/skill |
|---|---|---|---|---|---|---|---|---|---|---|
| ensemble | 32 | 100 | 160.98 | 510.94 | n/a | 0.0787 | 0.2857 | 0.0280 | 86.3 | 1.03 |
| deterministic | 1 | 100 | 185.93 | 540.49 | n/a | 0.2420 | 2.2287 | 0.2420 | 505.1 | 0.00 |

| EmberCast vs | Mean diff | 95% CI | CI excludes 0 | CVaR95 diff | CVaR95 95% CI | Win rate | Tie rate |
|---|---|---|---|---|---|---|---|
| deterministic | -24.95 | [-34.91, -15.60] | yes | -29.55 | [-72.59, 17.31] | 0.59 | 0.22 |

## Tag `ablation_lambda0`, stress `none`

| Policy | K | Seeds | Mean loss | CVaR95 loss | Regret vs oracle | Brier | Log loss | ECE | CRPS area | Spread/skill |
|---|---|---|---|---|---|---|---|---|---|---|
| ensemble | 64 | 100 | 158.61 | 502.80 | n/a | 0.0751 | 0.2540 | 0.0236 | 79.3 | 1.14 |
| deterministic | 1 | 100 | 185.93 | 540.49 | n/a | 0.2420 | 2.2287 | 0.2420 | 505.1 | 0.00 |

| EmberCast vs | Mean diff | 95% CI | CI excludes 0 | CVaR95 diff | CVaR95 95% CI | Win rate | Tie rate |
|---|---|---|---|---|---|---|---|
| deterministic | -27.31 | [-37.08, -18.00] | yes | -37.69 | [-88.45, 11.48] | 0.59 | 0.24 |

## Tag `ablation_nofilter`, stress `none`

| Policy | K | Seeds | Mean loss | CVaR95 loss | Regret vs oracle | Brier | Log loss | ECE | CRPS area | Spread/skill |
|---|---|---|---|---|---|---|---|---|---|---|
| ensemble | 64 | 100 | 185.76 | 494.71 | n/a | 0.1561 | 0.4972 | 0.1021 | 357.4 | 0.79 |
| deterministic | 1 | 100 | 185.93 | 540.49 | n/a | 0.2420 | 2.2287 | 0.2420 | 505.1 | 0.00 |

| EmberCast vs | Mean diff | 95% CI | CI excludes 0 | CVaR95 diff | CVaR95 95% CI | Win rate | Tie rate |
|---|---|---|---|---|---|---|---|
| deterministic | -0.17 | [-9.87, 9.94] | no | -45.78 | [-93.14, 13.10] | 0.34 | 0.22 |

## Tag `main`, stress `none`

| Policy | K | Seeds | Mean loss | CVaR95 loss | Regret vs oracle | Brier | Log loss | ECE | CRPS area | Spread/skill |
|---|---|---|---|---|---|---|---|---|---|---|
| ensemble | 64 | 100 | 159.66 | 502.11 | 12.70 | 0.0752 | 0.2559 | 0.0231 | 80.0 | 1.13 |
| deterministic | 1 | 100 | 185.93 | 540.49 | 38.98 | 0.2420 | 2.2287 | 0.2420 | 505.1 | 0.00 |
| front_nearest | 1 | 100 | 190.80 | 577.16 | 43.84 | n/a | n/a | n/a | n/a | n/a |
| oracle | 1 | 100 | 146.95 | 492.29 | 0.00 | n/a | n/a | n/a | n/a | n/a |

| EmberCast vs | Mean diff | 95% CI | CI excludes 0 | CVaR95 diff | CVaR95 95% CI | Win rate | Tie rate |
|---|---|---|---|---|---|---|---|
| deterministic | -26.27 | [-36.63, -16.35] | yes | -38.38 | [-94.68, 19.18] | 0.59 | 0.23 |
| front_nearest | -31.14 | [-40.15, -22.57] | yes | -75.05 | [-128.00, -22.56] | 0.71 | 0.22 |
| oracle | 12.70 | [6.46, 19.52] | yes | 9.82 | [-8.72, 52.40] | 0.21 | 0.26 |

## Tag `stress`, stress `dropout30`

| Policy | K | Seeds | Mean loss | CVaR95 loss | Regret vs oracle | Brier | Log loss | ECE | CRPS area | Spread/skill |
|---|---|---|---|---|---|---|---|---|---|---|
| ensemble | 64 | 100 | 149.40 | 513.63 | 13.92 | 0.0718 | 0.2410 | 0.0230 | 76.2 | 1.19 |
| deterministic | 1 | 100 | 175.61 | 553.37 | 40.13 | 0.2402 | 2.2128 | 0.2402 | 492.6 | 0.00 |
| front_nearest | 1 | 100 | 177.66 | 571.02 | 42.18 | n/a | n/a | n/a | n/a | n/a |
| oracle | 1 | 100 | 135.48 | 492.12 | 0.00 | n/a | n/a | n/a | n/a | n/a |

| EmberCast vs | Mean diff | 95% CI | CI excludes 0 | CVaR95 diff | CVaR95 95% CI | Win rate | Tie rate |
|---|---|---|---|---|---|---|---|
| deterministic | -26.21 | [-35.79, -17.29] | yes | -39.74 | [-89.90, 12.09] | 0.61 | 0.24 |
| front_nearest | -28.26 | [-36.94, -20.32] | yes | -57.39 | [-84.68, -26.29] | 0.65 | 0.24 |
| oracle | 13.92 | [8.58, 19.69] | yes | 21.51 | [-1.81, 55.57] | 0.19 | 0.26 |

## Tag `stress`, stress `late_scans`

| Policy | K | Seeds | Mean loss | CVaR95 loss | Regret vs oracle | Brier | Log loss | ECE | CRPS area | Spread/skill |
|---|---|---|---|---|---|---|---|---|---|---|
| ensemble | 64 | 100 | 148.77 | 510.56 | 13.29 | 0.0787 | 0.2629 | 0.0163 | 91.4 | 1.05 |
| deterministic | 1 | 100 | 174.70 | 519.63 | 39.21 | 0.2460 | 2.2662 | 0.2460 | 511.2 | 0.00 |
| front_nearest | 1 | 100 | 177.87 | 561.74 | 42.38 | n/a | n/a | n/a | n/a | n/a |
| oracle | 1 | 100 | 135.48 | 492.12 | 0.00 | n/a | n/a | n/a | n/a | n/a |

| EmberCast vs | Mean diff | 95% CI | CI excludes 0 | CVaR95 diff | CVaR95 95% CI | Win rate | Tie rate |
|---|---|---|---|---|---|---|---|
| deterministic | -25.92 | [-34.78, -17.81] | yes | -9.06 | [-45.26, 21.81] | 0.62 | 0.22 |
| front_nearest | -29.09 | [-37.65, -20.84] | yes | -51.18 | [-92.66, -9.75] | 0.67 | 0.23 |
| oracle | 13.29 | [7.92, 19.34] | yes | 18.45 | [-10.03, 53.18] | 0.18 | 0.25 |

## Tag `stress`, stress `regime`

| Policy | K | Seeds | Mean loss | CVaR95 loss | Regret vs oracle | Brier | Log loss | ECE | CRPS area | Spread/skill |
|---|---|---|---|---|---|---|---|---|---|---|
| ensemble | 64 | 100 | 177.06 | 528.55 | 14.56 | 0.0794 | 0.2730 | 0.0264 | 82.1 | 1.08 |
| deterministic | 1 | 100 | 202.60 | 553.08 | 40.10 | 0.2420 | 2.2286 | 0.2420 | 493.3 | 0.00 |
| front_nearest | 1 | 100 | 202.57 | 607.88 | 40.07 | n/a | n/a | n/a | n/a | n/a |
| oracle | 1 | 100 | 162.50 | 528.20 | 0.00 | n/a | n/a | n/a | n/a | n/a |

| EmberCast vs | Mean diff | 95% CI | CI excludes 0 | CVaR95 diff | CVaR95 95% CI | Win rate | Tie rate |
|---|---|---|---|---|---|---|---|
| deterministic | -25.54 | [-34.80, -16.79] | yes | -24.53 | [-62.02, 0.65] | 0.61 | 0.19 |
| front_nearest | -25.52 | [-33.72, -17.70] | yes | -79.34 | [-132.87, -35.45] | 0.67 | 0.19 |
| oracle | 14.56 | [8.70, 20.88] | yes | 0.35 | [-11.25, 25.46] | 0.21 | 0.23 |

## Tag `stress`, stress `stuck`

| Policy | K | Seeds | Mean loss | CVaR95 loss | Regret vs oracle | Brier | Log loss | ECE | CRPS area | Spread/skill |
|---|---|---|---|---|---|---|---|---|---|---|
| ensemble | 64 | 100 | 151.95 | 514.30 | 16.47 | 0.0720 | 0.2394 | 0.0237 | 76.2 | 1.29 |
| deterministic | 1 | 100 | 174.47 | 523.79 | 38.99 | 0.2419 | 2.2281 | 0.2419 | 494.8 | 0.00 |
| front_nearest | 1 | 100 | 177.66 | 571.02 | 42.18 | n/a | n/a | n/a | n/a | n/a |
| oracle | 1 | 100 | 135.48 | 492.12 | 0.00 | n/a | n/a | n/a | n/a | n/a |

| EmberCast vs | Mean diff | 95% CI | CI excludes 0 | CVaR95 diff | CVaR95 95% CI | Win rate | Tie rate |
|---|---|---|---|---|---|---|---|
| deterministic | -22.52 | [-32.36, -12.97] | yes | -9.49 | [-53.38, 24.10] | 0.58 | 0.24 |
| front_nearest | -25.71 | [-34.40, -17.31] | yes | -56.72 | [-84.14, -22.88] | 0.65 | 0.26 |
| oracle | 16.47 | [10.71, 22.89] | yes | 22.18 | [4.65, 53.66] | 0.14 | 0.25 |

