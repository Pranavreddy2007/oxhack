# EmberCast Architecture

Status: stub (Step 1). Later steps fill each section.

## 1. Data flow (live)
```
sim_stream (laptop) --TCP--> board_daemon --TCP--> dashboard
MCU --Bridge--> mcu_sidecar.py --UDS--> board_daemon
board_daemon --UDS--> mcu_sidecar.py --Bridge--> MCU (status)
```
Laptop-only mode: sim_stream, engine, and dashboard run in one process or
on localhost.

## 2. Per-tick engine pipeline
observations -> feature update -> particle filter -> forecast (H ticks)
-> prediction distribution -> uncertainty -> candidate actions
-> expected utility (E[loss], CVaR) -> selected plan.

## 3. Interfaces
| Interface | Header | Implementations |
|---|---|---|
| ITransport | transport.hpp | InProcess, TcpTransport, UdsTransport |
| IForecastBackend | forecast.hpp | cpu_naive, cpu_opt, opencl (optional) |
| IPolicy | policy.hpp | EnsembleRisk, FrontNearest (B0), Deterministic (B1), Oracle |
| ISensorSource | transport.hpp | MCU via sidecar, laptop emulation |
| IStatusSink | transport.hpp | MCU display via sidecar, console |

## 4. Threads
To be written in Step 6 and Step 7: receiver threads, SPSC ring buffer,
engine thread, thread pool and per-tick barrier, log writer thread.

## 5. Frame format
See PRD.md Section 8.
