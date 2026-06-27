# FocoFlow

**Predictive focus management for ADHD brains — one local desktop app.**

FocoFlow measures not just *whether* you're working, but *how*: deep work, drift, context-switch thrash. When you return from a distraction, **Snapback** shows where you left off so context switches hurt less.

![Status](https://img.shields.io/badge/status-alpha-yellow)
![Platform](https://img.shields.io/badge/platform-macOS%20%7C%20Windows%20%7C%20Linux-blue)
![License](https://img.shields.io/badge/license-MIT-green)

## Architecture (v0.2)

```
Rust core (Tauri) — capture + features + classifier + SQLite + snapback overlay
        ↕ invoke / events
React dashboard (Vite + TypeScript)

Python (offline only) — train on labeled sessions → export ONNX
```

The old 4-layer stack (C++ → ZeroMQ → Python service → Spring Boot → WebSocket) has been removed. One signed desktop binary, local-first, no network API tier.

## Features

- **Live focus states:** `DEEP_FOCUS`, `PRODUCTIVE`, `PSEUDO_PRODUCTIVE` (drift), `DISTRACTED`
- **Snapback overlay:** "You were editing auth.ts in focoflow" when you return from distraction
- **Focus modes:** deep / normal / recovery (different risk thresholds + hyperfocus guardrails)
- **One-tap feedback:** label moments to build a personal training set
- **Session recap:** duration, avg focus, deep-work %, snapback count, thrash spikes
- **SQLite persistence:** sessions, predictions, labels, context snapshots

## Project structure

```
FocoFlow/
├── src-tauri/              # Rust core (capture, engine, storage, snapback)
├── frontend/               # React dashboard + snapback.html overlay
├── ml/                     # Offline training + ONNX export
├── docs/                   # Design reference (historical architecture docs)
├── tools/generate_log.py   # Synthetic event log for training experiments
├── samples/events_demo.bin # Demo binary log (legacy schema)
└── package.json            # Tauri scripts
```

## Quick start

### Prerequisites

- **Rust** 1.77+ ([rustup](https://rustup.rs))
- **Node.js** 18+
- **macOS:** grant **Accessibility** + **Input Monitoring** to FocoFlow in System Settings

### Dev

```bash
npm install
cd frontend && npm install && cd ..

# Run the desktop app (Rust + Vite)
npm run tauri:dev
```

### Build

```bash
npm run tauri:build
```

### Offline ML (optional)

```bash
cd ml
python -m venv venv && source venv/bin/activate
pip install xgboost skl2onnx onnx  # optional backends

# Train from labeled feature CSVs, then export:
python -m ml.train_cli --help
python -m ml.export_onnx --model-path artifacts/model.json --output artifacts/model.onnx

# Run Rust with ONNX (when wired):
# cd src-tauri && cargo build --features onnx
```

## Permissions (macOS)

Global input capture and active-window detection require system permissions. If capture shows as idle:

1. Open **System Settings → Privacy & Security**
2. Enable **Accessibility** for FocoFlow
3. Enable **Input Monitoring** for FocoFlow
4. Click **Refresh permissions** in the app

## CI

`.github/workflows/ci.yml` runs Python tests, frontend typecheck/tests, and `cargo check` + `cargo test` for the Rust core.

## Roadmap

- [x] Collapse to Tauri desktop app
- [x] Snapback overlay + SQLite persistence
- [x] Focus modes + session recap + feedback labels
- [ ] Ship ONNX inference in Rust (`--features onnx`)
- [ ] Windows/Linux permission UX polish
- [ ] Workflow training ground (parked — revisit after core loop is sticky)

## License

MIT
