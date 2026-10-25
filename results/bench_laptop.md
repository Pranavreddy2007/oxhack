# Laptop (development container) benchmark tables

Made by `laptop/tools/bench_report.py` from: `results/raw/bench/cpu_naive_t1_N96_K64_H20_raw.csv`, `results/raw/bench/cpu_opt_t1_N96_K64_H20_raw.csv`, `results/raw/bench/cpu_opt_t2_N96_K64_H20_raw.csv`, `results/raw/bench/cpu_opt_t4_N96_K64_H20_raw.csv`

| Run | Stage | Count | Mean ms | p50 ms | p95 ms | p99 ms | p99.9 ms | Max ms |
|---|---|---|---|---|---|---|---|---|
| cpu_naive_t1_N96_K64_H20 | decision_cold_episode | 9 | 363.577 | 338.432 | 521.302 | 521.302 | n/a (count < 10000) | 521.302 |
| cpu_naive_t1_N96_K64_H20 | decision_cold_process | 1 | 308.750 | 308.750 | 308.750 | 308.750 | n/a (count < 10000) | 308.750 |
| cpu_naive_t1_N96_K64_H20 | decision_policy | 230 | 0.080 | 0.079 | 0.119 | 0.131 | n/a (count < 10000) | 0.181 |
| cpu_naive_t1_N96_K64_H20 | decision_rollout | 230 | 305.991 | 297.912 | 424.848 | 528.098 | n/a (count < 10000) | 542.653 |
| cpu_naive_t1_N96_K64_H20 | decision_total | 230 | 322.498 | 314.581 | 441.433 | 556.313 | n/a (count < 10000) | 571.803 |
| cpu_naive_t1_N96_K64_H20 | tick_filter | 1190 | 16.352 | 15.662 | 25.938 | 29.041 | n/a (count < 10000) | 34.159 |
| cpu_opt_t1_N96_K64_H20 | decision_cold_episode | 9 | 10.565 | 9.211 | 15.241 | 15.241 | n/a (count < 10000) | 15.241 |
| cpu_opt_t1_N96_K64_H20 | decision_cold_process | 1 | 12.446 | 12.446 | 12.446 | 12.446 | n/a (count < 10000) | 12.446 |
| cpu_opt_t1_N96_K64_H20 | decision_policy | 230 | 0.080 | 0.076 | 0.138 | 0.173 | n/a (count < 10000) | 0.175 |
| cpu_opt_t1_N96_K64_H20 | decision_rollout | 230 | 31.814 | 28.573 | 63.529 | 93.128 | n/a (count < 10000) | 123.641 |
| cpu_opt_t1_N96_K64_H20 | decision_total | 230 | 34.506 | 30.932 | 66.370 | 99.023 | n/a (count < 10000) | 128.644 |
| cpu_opt_t1_N96_K64_H20 | tick_filter | 1190 | 2.614 | 2.480 | 4.958 | 7.151 | n/a (count < 10000) | 9.193 |
| cpu_opt_t2_N96_K64_H20 | decision_cold_episode | 9 | 6.851 | 6.313 | 10.182 | 10.182 | n/a (count < 10000) | 10.182 |
| cpu_opt_t2_N96_K64_H20 | decision_cold_process | 1 | 8.568 | 8.568 | 8.568 | 8.568 | n/a (count < 10000) | 8.568 |
| cpu_opt_t2_N96_K64_H20 | decision_policy | 230 | 0.077 | 0.073 | 0.137 | 0.160 | n/a (count < 10000) | 0.187 |
| cpu_opt_t2_N96_K64_H20 | decision_rollout | 230 | 17.134 | 15.332 | 38.277 | 49.244 | n/a (count < 10000) | 52.371 |
| cpu_opt_t2_N96_K64_H20 | decision_total | 230 | 19.035 | 17.078 | 40.983 | 52.271 | n/a (count < 10000) | 55.843 |
| cpu_opt_t2_N96_K64_H20 | tick_filter | 1190 | 1.856 | 1.674 | 3.380 | 4.556 | n/a (count < 10000) | 5.241 |
| cpu_opt_t4_N96_K64_H20 | decision_cold_episode | 9 | 4.452 | 4.246 | 5.357 | 5.357 | n/a (count < 10000) | 5.357 |
| cpu_opt_t4_N96_K64_H20 | decision_cold_process | 1 | 4.204 | 4.204 | 4.204 | 4.204 | n/a (count < 10000) | 4.204 |
| cpu_opt_t4_N96_K64_H20 | decision_policy | 230 | 0.085 | 0.081 | 0.130 | 0.172 | n/a (count < 10000) | 0.231 |
| cpu_opt_t4_N96_K64_H20 | decision_rollout | 230 | 9.937 | 8.887 | 19.869 | 27.248 | n/a (count < 10000) | 30.539 |
| cpu_opt_t4_N96_K64_H20 | decision_total | 230 | 11.525 | 10.306 | 22.324 | 29.808 | n/a (count < 10000) | 33.284 |
| cpu_opt_t4_N96_K64_H20 | tick_filter | 1190 | 1.532 | 1.349 | 2.736 | 3.909 | n/a (count < 10000) | 5.250 |

| Run | Backend | Threads req. | HW threads | N | K | H | Wall s | CPU util (cores) | Peak RSS kB | Rollout member-ticks/s | Rollout cell-updates/s |
|---|---|---|---|---|---|---|---|---|---|---|---|
| cpu_naive_t1_N96_K64_H20 | cpu_naive | 1 | 4 | 96 | 64 | 20 | 93.89 | 0.99 | 5828 | 4.18e+03 | 3.86e+07 |
| cpu_opt_t1_N96_K64_H20 | cpu_opt | 1 | 4 | 96 | 64 | 20 | 11.02 | 0.99 | 6544 | 4.02e+04 | 3.71e+08 |
| cpu_opt_t2_N96_K64_H20 | cpu_opt | 2 | 4 | 96 | 64 | 20 | 6.69 | 1.74 | 6980 | 7.47e+04 | 6.88e+08 |
| cpu_opt_t4_N96_K64_H20 | cpu_opt | 4 | 4 | 96 | 64 | 20 | 4.69 | 2.75 | 6852 | 1.29e+05 | 1.19e+09 |
