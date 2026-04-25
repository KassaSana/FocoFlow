# FocoFlow: Progress

A short, dated changelog of the project. The full session-by-session journal
lives at [docs/archive/PROGRESS_full.md](docs/archive/PROGRESS_full.md).

---

## 2026-04 — Cleanup pass

- Repo renamed to **FocoFlow** at the user-facing string layer (titles,
  package names, dashboard header). Stable identifiers (`com.neurofocus`
  Java package, `neurofocus_engine` CMake target, Maven `groupId`/
  `artifactId`, CORS env var) intentionally left alone.
- Project structure trimmed: experimental context-recovery C++ headers
  moved under `core/experimental/`; `generate_log.py` moved into `tools/`;
  a committed demo log (`samples/events_demo.bin`) added so the replay
  flow works on a fresh clone.
- README rebuilt to match the actual file layout; broken doc links
  removed; defunct `events_test_2026-01-02.log` references replaced
  with `samples/events_demo.bin`.
- Backend now ships an `application.properties` (`server.port=8080`,
  `spring.application.name=focoflow-backend`).
- 600-line `PROGRESS.md` archived (with mojibake fix) and replaced with
  this changelog.

## 2026-Q1 — Frontend dashboard

- React + Vite + TypeScript dashboard under `frontend/`.
- Live focus score + distraction risk via REST (`/api/predictions/latest`)
  and a WebSocket stream (`/ws/predictions`) with reconnect backoff.
- Session goal entry, session metadata fetch, history strip.

## 2026-Q1 — Spring Boot backend

- REST controllers for health, predictions, sessions.
- WebSocket handler for the live prediction stream.
- 9/9 tests green.

## 2025-Q4 / 2026-Q1 — Python ML pipeline

- Binary event log reader matching the C++ schema.
- Rolling-window feature extraction (`features.py`).
- Baseline classifier training (`training_pipeline.py`, `train_cli.py`).
- ZMQ subscriber → REST/WS bridge (`inference_server.py`).
- Log-replay tool for cross-platform demos (`event_replay.py`).
- 23/23 tests green.

## 2025-Q4 — C++ event capture engine (Windows)

- Win32 low-level hook capture (keyboard, mouse, window focus).
- Lock-free SPSC ring buffer.
- Memory-mapped event log persistence.
- ZeroMQ publisher.
- End-to-end runner: `neurofocus_engine.cpp`.

## 2025-Q4 — Design docs

- Technical Design Document, Architecture, Concepts, Schemas, Context
  Recovery Design, Metrics, Deployment — all under `docs/`.
