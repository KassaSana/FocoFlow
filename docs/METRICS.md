# Metrics Snapshot

This file tracks project metrics and explains what each metric measures so you can reference them in a resume.

## Log-Based Metrics (event replay)

Generated from the bundled log with:

```powershell
python -m ml.metrics_report --log-path .\events_test_2026-01-02.log --benchmark-features --output-json docs\metrics.json
```

Current snapshot (from `docs/metrics.json`):

```text
log_path: .\events_test_2026-01-02.log
total_events: 100
duration_seconds: 9.90
events_per_second: 10.10
unique_apps: 1
event_type_breakdown:
  MOUSE_MOVE: 100
feature_extraction_eps: 6467.01
```

What these metrics track:
- total_events: count of raw event records in the log.
- duration_seconds: time span between first and last event timestamps.
- events_per_second: observed event rate in the log (not a max throughput benchmark).
- unique_apps: number of distinct app names seen in the log.
- event_type_breakdown: count per event type (keyboard, mouse, etc.).
- feature_extraction_eps: feature vectors processed per second on this machine.

Raw metrics JSON: `docs/metrics.json`

## Core Performance Benchmark (ring buffer)

Run the low-level benchmark:

```powershell
.\tools\benchmark.exe
```

Current snapshot (most recent run):

```text
try_push: p50=0.00 ns  p99=135.50 ns  p999=152.95 ns  n=200
try_pop : p50=0.00 ns  p99=135.86 ns  p999=183.47 ns  n=200
end_to_end: p50=121.70 ns  p99=183.59 ns  p999=183.84 ns  n=200
throughput: 5734371 ev/s (push+pop single-thread)
```

What these metrics track:
- try_push/try_pop: latency per ring buffer operation (ns).
- end_to_end: push+pop latency per event (ns).
- throughput: single-thread event rate for push+pop loop (events/sec).

## 2026-01-04 Verification Runs

Log metrics runs (command above, ran three times):

```text
run_1: feature_extraction_eps: 6467.01
run_2: feature_extraction_eps: 6383.25
run_3: feature_extraction_eps: 6466.30
```

Core benchmark runs (.\tools\benchmark.exe, ran three times):

```text
run_1: try_push p99=1404.91 ns, try_pop p99=1101.44 ns, end_to_end p99=1405.03 ns, throughput=5749524 ev/s
run_2: try_push p99=1404.79 ns, try_pop p99=348.14 ns,  end_to_end p99=1106.57 ns, throughput=5737280 ev/s
run_3: try_push p99=135.50 ns,  try_pop p99=135.86 ns,  end_to_end p99=183.59 ns,  throughput=5734371 ev/s
```

## Resume-Ready Bullet Ideas

- Achieved 5.73-5.75M events/sec single-thread throughput in the lock-free ring buffer benchmark (2026-01-04).
- p99 ring buffer operation latency measured between 0.14-1.40 microseconds (local benchmark, 2026-01-04).
- Feature extraction throughput 6.38k-6.47k events/sec on recorded log replay (Python pipeline, 2026-01-04).
