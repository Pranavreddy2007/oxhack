# UNO Q benchmark tables

Made by `laptop/tools/bench_report.py` from: `results/raw/board/cpu_naive_t1_N96_K64_H20_raw.csv`, `results/raw/board/cpu_opt_t1_N96_K64_H20_raw.csv`, `results/raw/board/cpu_opt_t2_N96_K64_H20_raw.csv`, `results/raw/board/cpu_opt_t4_N96_K128_H20_raw.csv`, `results/raw/board/cpu_opt_t4_N96_K256_H20_raw.csv`, `results/raw/board/cpu_opt_t4_N96_K32_H20_raw.csv`, `results/raw/board/cpu_opt_t4_N96_K64_H20_raw.csv`, `results/raw/board/opencl_t0_N96_K64_H20_raw.csv`

| Run | Stage | Count | Mean ms | p50 ms | p95 ms | p99 ms | p99.9 ms | Max ms |
|---|---|---|---|---|---|---|---|---|
| cpu_naive_t1_N96_K64_H20 | decision_cold_episode | 4 | 995.084 | 993.190 | 997.650 | 997.650 | n/a (count < 10000) | 997.650 |
| cpu_naive_t1_N96_K64_H20 | decision_cold_process | 1 | 993.788 | 993.788 | 993.788 | 993.788 | n/a (count < 10000) | 993.788 |
| cpu_naive_t1_N96_K64_H20 | decision_policy | 115 | 0.261 | 0.259 | 0.298 | 0.307 | n/a (count < 10000) | 0.311 |
| cpu_naive_t1_N96_K64_H20 | decision_rollout | 115 | 826.546 | 852.539 | 986.081 | 990.763 | n/a (count < 10000) | 990.957 |
| cpu_naive_t1_N96_K64_H20 | decision_total | 115 | 873.589 | 902.969 | 1039.145 | 1043.806 | n/a (count < 10000) | 1044.191 |
| cpu_naive_t1_N96_K64_H20 | tick_filter | 595 | 46.741 | 48.367 | 53.773 | 54.374 | n/a (count < 10000) | 54.493 |
| cpu_opt_t1_N96_K64_H20 | decision_cold_episode | 99 | 23.953 | 23.824 | 31.231 | 44.745 | n/a (count < 10000) | 44.745 |
| cpu_opt_t1_N96_K64_H20 | decision_cold_process | 1 | 29.210 | 29.210 | 29.210 | 29.210 | n/a (count < 10000) | 29.210 |
| cpu_opt_t1_N96_K64_H20 | decision_policy | 2300 | 0.246 | 0.255 | 0.281 | 0.314 | n/a (count < 10000) | 0.362 |
| cpu_opt_t1_N96_K64_H20 | decision_rollout | 2300 | 56.734 | 46.149 | 137.426 | 176.042 | n/a (count < 10000) | 220.413 |
| cpu_opt_t1_N96_K64_H20 | decision_total | 2300 | 63.724 | 52.506 | 149.239 | 191.237 | n/a (count < 10000) | 236.790 |
| cpu_opt_t1_N96_K64_H20 | tick_filter | 11900 | 6.746 | 6.157 | 11.606 | 14.155 | 17.271 | 18.362 |
| cpu_opt_t2_N96_K64_H20 | decision_cold_episode | 99 | 13.943 | 13.818 | 17.647 | 24.993 | n/a (count < 10000) | 24.993 |
| cpu_opt_t2_N96_K64_H20 | decision_cold_process | 1 | 16.799 | 16.799 | 16.799 | 16.799 | n/a (count < 10000) | 16.799 |
| cpu_opt_t2_N96_K64_H20 | decision_policy | 2300 | 0.249 | 0.255 | 0.304 | 0.341 | n/a (count < 10000) | 0.421 |
| cpu_opt_t2_N96_K64_H20 | decision_rollout | 2300 | 29.827 | 24.542 | 70.935 | 89.039 | n/a (count < 10000) | 112.229 |
| cpu_opt_t2_N96_K64_H20 | decision_total | 2300 | 34.664 | 28.960 | 78.581 | 98.054 | n/a (count < 10000) | 123.281 |
| cpu_opt_t2_N96_K64_H20 | tick_filter | 11900 | 4.608 | 4.333 | 7.484 | 9.368 | 11.305 | 11.963 |
| cpu_opt_t4_N96_K128_H20 | decision_cold_episode | 19 | 18.169 | 18.134 | 22.675 | 22.675 | n/a (count < 10000) | 22.675 |
| cpu_opt_t4_N96_K128_H20 | decision_cold_process | 1 | 20.116 | 20.116 | 20.116 | 20.116 | n/a (count < 10000) | 20.116 |
| cpu_opt_t4_N96_K128_H20 | decision_policy | 460 | 0.520 | 0.534 | 0.580 | 0.613 | n/a (count < 10000) | 0.639 |
| cpu_opt_t4_N96_K128_H20 | decision_rollout | 460 | 35.478 | 30.083 | 73.636 | 108.472 | n/a (count < 10000) | 114.834 |
| cpu_opt_t4_N96_K128_H20 | decision_total | 460 | 43.012 | 36.970 | 86.223 | 118.951 | n/a (count < 10000) | 131.296 |
| cpu_opt_t4_N96_K128_H20 | tick_filter | 2380 | 7.054 | 6.424 | 11.442 | 14.433 | n/a (count < 10000) | 18.089 |
| cpu_opt_t4_N96_K256_H20 | decision_cold_episode | 19 | 35.694 | 35.433 | 45.057 | 45.057 | n/a (count < 10000) | 45.057 |
| cpu_opt_t4_N96_K256_H20 | decision_cold_process | 1 | 40.680 | 40.680 | 40.680 | 40.680 | n/a (count < 10000) | 40.680 |
| cpu_opt_t4_N96_K256_H20 | decision_policy | 460 | 1.089 | 1.114 | 1.172 | 1.193 | n/a (count < 10000) | 1.204 |
| cpu_opt_t4_N96_K256_H20 | decision_rollout | 460 | 69.878 | 58.690 | 142.393 | 215.525 | n/a (count < 10000) | 227.475 |
| cpu_opt_t4_N96_K256_H20 | decision_total | 460 | 84.647 | 71.903 | 160.915 | 236.504 | n/a (count < 10000) | 261.016 |
| cpu_opt_t4_N96_K256_H20 | tick_filter | 2380 | 13.778 | 12.554 | 22.414 | 28.072 | n/a (count < 10000) | 35.780 |
| cpu_opt_t4_N96_K32_H20 | decision_cold_episode | 19 | 5.067 | 4.950 | 6.376 | 6.376 | n/a (count < 10000) | 6.376 |
| cpu_opt_t4_N96_K32_H20 | decision_cold_process | 1 | 6.583 | 6.583 | 6.583 | 6.583 | n/a (count < 10000) | 6.583 |
| cpu_opt_t4_N96_K32_H20 | decision_policy | 460 | 0.128 | 0.128 | 0.166 | 0.197 | n/a (count < 10000) | 0.254 |
| cpu_opt_t4_N96_K32_H20 | decision_rollout | 460 | 9.407 | 8.076 | 19.123 | 27.886 | n/a (count < 10000) | 29.629 |
| cpu_opt_t4_N96_K32_H20 | decision_total | 460 | 11.401 | 10.035 | 21.974 | 30.579 | n/a (count < 10000) | 34.266 |
| cpu_opt_t4_N96_K32_H20 | tick_filter | 2380 | 1.870 | 1.744 | 2.986 | 3.724 | n/a (count < 10000) | 4.613 |
| cpu_opt_t4_N96_K64_H20 | decision_cold_episode | 19 | 9.389 | 9.283 | 11.919 | 11.919 | n/a (count < 10000) | 11.919 |
| cpu_opt_t4_N96_K64_H20 | decision_cold_process | 1 | 11.552 | 11.552 | 11.552 | 11.552 | n/a (count < 10000) | 11.552 |
| cpu_opt_t4_N96_K64_H20 | decision_policy | 460 | 0.256 | 0.259 | 0.300 | 0.347 | n/a (count < 10000) | 0.374 |
| cpu_opt_t4_N96_K64_H20 | decision_rollout | 460 | 18.034 | 15.423 | 36.673 | 54.845 | n/a (count < 10000) | 58.883 |
| cpu_opt_t4_N96_K64_H20 | decision_total | 460 | 21.930 | 18.992 | 41.606 | 60.614 | n/a (count < 10000) | 67.005 |
| cpu_opt_t4_N96_K64_H20 | tick_filter | 2380 | 3.659 | 3.346 | 5.842 | 7.255 | n/a (count < 10000) | 9.031 |
| opencl_t0_N96_K64_H20 | decision_cold_episode | 2 | 1090.889 | 1034.133 | 1147.646 | 1147.646 | n/a (count < 10000) | 1147.646 |
| opencl_t0_N96_K64_H20 | decision_cold_process | 1 | 1025.628 | 1025.628 | 1025.628 | 1025.628 | n/a (count < 10000) | 1025.628 |
| opencl_t0_N96_K64_H20 | decision_policy | 69 | 0.778 | 0.808 | 0.857 | 0.933 | n/a (count < 10000) | 0.933 |
| opencl_t0_N96_K64_H20 | decision_rollout | 69 | 1940.495 | 1962.319 | 2650.991 | 2773.655 | n/a (count < 10000) | 2773.655 |
| opencl_t0_N96_K64_H20 | decision_total | 69 | 2049.140 | 2064.623 | 2790.390 | 2923.192 | n/a (count < 10000) | 2923.192 |
| opencl_t0_N96_K64_H20 | tick_filter | 357 | 106.999 | 108.920 | 148.912 | 152.218 | n/a (count < 10000) | 156.311 |

| Run | Backend | Threads req. | HW threads | N | K | H | Wall s | CPU util (cores) | Peak RSS kB | Rollout member-ticks/s | Rollout cell-updates/s |
|---|---|---|---|---|---|---|---|---|---|---|---|
| cpu_naive_t1_N96_K64_H20 | cpu_naive | 1 | 4 | 96 | 64 | 20 | 128.55 | 0.98 | 3860 | 1.55e+03 | 1.43e+07 |
| cpu_opt_t1_N96_K64_H20 | cpu_opt | 1 | 4 | 96 | 64 | 20 | 227.66 | 0.99 | 4836 | 2.26e+04 | 2.08e+08 |
| cpu_opt_t2_N96_K64_H20 | cpu_opt | 2 | 4 | 96 | 64 | 20 | 139.60 | 1.64 | 5696 | 4.29e+04 | 3.95e+08 |
| cpu_opt_t4_N96_K128_H20 | cpu_opt | 4 | 4 | 96 | 128 | 20 | 36.58 | 2.62 | 7952 | 7.22e+04 | 6.65e+08 |
| cpu_opt_t4_N96_K256_H20 | cpu_opt | 4 | 4 | 96 | 256 | 20 | 69.13 | 2.71 | 12208 | 7.33e+04 | 6.75e+08 |
| cpu_opt_t4_N96_K32_H20 | cpu_opt | 4 | 4 | 96 | 32 | 20 | 11.75 | 2.26 | 3928 | 6.8e+04 | 6.27e+08 |
| cpu_opt_t4_N96_K64_H20 | cpu_opt | 4 | 4 | 96 | 64 | 20 | 20.16 | 2.46 | 5200 | 7.1e+04 | 6.54e+08 |
| opencl_t0_N96_K64_H20 | opencl | 0 | 4 | 96 | 64 | 20 | 176.62 | 0.08 | 102756 | 660 | 6.08e+06 |

## Notes (2026-10-25)
Simulation only. Not for operational use.
- Board: Arduino UNO Q ("Imola"), 4 cores 300-2016 MHz (schedutil),
  3.6 GiB RAM, Debian 13, kernel 6.16.7. Binaries cross-built with zig,
  `-O3 -mcpu=cortex-a53` (D-025). Threads were not pinned.
- Commands: Step 10a in `scripts/print_board_build.sh`, run over adb.
  `--episodes 100` for the t1/t2 runs, `--episodes 20` for the K sweep.
  The t4 K=64 file is the K-sweep run (it has the same file name as the
  t4 100-episode run and replaced it; that run gave mean 20.5 ms, p95 43.4 ms).
- opencl: `--backend opencl --episodes 3` on the Adreno 702 (rusticl).
  It is bit-identical to cpu_naive but about 90x slower than cpu_opt t4 (D-020).
- Thermal (cpuss0 / gpu, deg C): 37.3 / 36.3 before; 52.6 / 47.0 after the
  t4 run; 52.9 / 48.2 at the end (12 min of load). scaling_cur_freq read
  between runs: 1420800 to 1612800 kHz. The clock under load was not sampled.
- Budget: the demo tick is 250 ms. cpu_opt t4 at K = 64 has p99 60.6 ms.
  K = 256 has p99 236.5 ms, which is too close to the tick. K = 64 is kept.
