# CVaR sweep: lambda x alpha (TUNE seeds only)

Simulation only. Not for operational use.

Made with `embercast_replay --set tune` (40 seeds). TEST seeds were not used.
CVaR95 over 40 episodes is the mean of the worst 2 episodes. CVaR90 is the mean of the worst 4.

| Run | Mean loss | CVaR95 loss | CVaR90 loss | Mean diff vs B1 |
|---|---|---|---|---|
| deterministic (B1) | 135.90 | 656.22 | 574.07 | n/a |
| front_nearest (B0) | 143.68 | 660.67 | 576.73 | n/a |
| ensemble lambda=0, alpha=0.8 | 121.53 | 651.49 | 563.99 | -14.37 |
| ensemble lambda=0, alpha=0.9 | 121.53 | 651.49 | 563.99 | -14.37 |
| ensemble lambda=0, alpha=0.95 | 121.53 | 651.49 | 563.99 | -14.37 |
| ensemble lambda=0.5, alpha=0.8 | 120.01 | 655.29 | 567.22 | -15.89 |
| ensemble lambda=0.5, alpha=0.9 | 118.26 | 654.72 | 568.90 | -17.64 |
| ensemble lambda=0.5, alpha=0.95 | 119.30 | 653.47 | 568.28 | -16.61 |
| ensemble lambda=1.5, alpha=0.8 | 119.30 | 656.99 | 568.35 | -16.61 |
| ensemble lambda=1.5, alpha=0.9 | 118.52 | 642.65 | 562.87 | -17.39 |
| ensemble lambda=1.5, alpha=0.95 | 119.80 | 655.23 | 569.16 | -16.10 |
| ensemble lambda=3, alpha=0.8 | 119.99 | 656.99 | 570.04 | -15.91 |
| ensemble lambda=3, alpha=0.9 | 118.73 | 652.35 | 567.72 | -17.17 |
| ensemble lambda=3, alpha=0.95 | 119.28 | 651.17 | 567.40 | -16.63 |
| ensemble lambda=6, alpha=0.8 | 120.42 | 653.59 | 568.34 | -15.49 |
| ensemble lambda=6, alpha=0.9 | 120.95 | 655.23 | 569.84 | -14.95 |
| ensemble lambda=6, alpha=0.95 | 121.04 | 651.17 | 568.08 | -14.87 |

Result: all settings are within about 15 loss units of each other on CVaR95 (about 2 % of the value).
This is less than the noise of a mean over 2 episodes. No setting was changed (D-024).
