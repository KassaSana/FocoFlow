# Benchmarks (resume-grade, defensible numbers)

This repo includes a built-in benchmark mode that prints **copy/paste-able metrics** you can cite in a resume (with the exact command used to produce them).

## Prereqs

- Install **Rust** (includes `cargo`) via `rustup` for Windows.
- From the repo root, run commands from `src-tauri/`.

## Inference latency benchmark (median / p95 / p99)

Runs the in-app `Classifier::predict()` in a tight loop and prints latency percentiles in **microseconds**.

```powershell
cd src-tauri
cargo run --release -- --benchmark --runs 20000 --warmup 2000
```

Optional: include a goal string (bench will report `goal_present=true`).

```powershell
cd src-tauri
cargo run --release -- --benchmark --runs 20000 --warmup 2000 --goal "implement feature X"
```

Example output (your values will differ):

```text
SNAPBACK_BENCH v1
mode=inference
runs=20000
warmup=2000
goal_present=false
latency_us_p50=35
latency_us_p95=62
latency_us_p99=90
mem_kib_before=12345
mem_kib_after=12510
cpu_pct_sample=0.80
bench_elapsed_ms=980
```

## Reliability / soak run (crash-free runtime)

Runs the same code path continuously and reports progress every 5 seconds. Cite only what you actually ran.

```powershell
cd src-tauri
cargo run --release -- --benchmark --soak-seconds 3600
```

The final lines include:

- `soak_seconds=...`
- `soak_iters=...`

## Startup timing (app launch)

When running the normal app, the backend logs startup milestones:

- `startup_ms_to_setup`: process start → Tauri `.setup()` entered
- `startup_ms_to_ready`: process start → Tauri `RunEvent::Ready`

Run the app and capture logs:

```powershell
cd src-tauri
$env:RUST_LOG="info"
cargo run
```

Then cite: “cold start to ready: **X ms** on **<your machine>**”.

