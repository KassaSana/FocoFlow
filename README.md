# Snapback

**Predictive focus management for ADHD brains — one local desktop app.**

Snapback measures not just *whether* you're working, but *how*: deep work, drift, context-switch thrash. When you return from a distraction, the **snapback overlay** shows where you left off so context switches hurt less.

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
- **Snapback overlay:** "You were editing auth.ts in Snapback" when you return from distraction
- **Focus modes:** deep / normal / recovery (different risk thresholds + hyperfocus guardrails)
- **One-tap feedback:** label moments to build a personal training set
- **Session recap:** duration, avg focus, deep-work %, snapback count, thrash spikes
- **SQLite persistence:** sessions, predictions, labels, context snapshots

## Project structure

```
Snapback/
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
- **macOS:** grant **Accessibility** + **Input Monitoring** to Snapback in System Settings

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
2. Enable **Accessibility** for Snapback
3. Enable **Input Monitoring** for Snapback
4. Click **Refresh permissions** in the app

## Troubleshooting local dev

### `No space left on device` during `npm run tauri:dev`

The first Tauri/Rust build is large (often **3–8 GB** of compile artifacts). If your disk is full, you'll see errors like:

```text
rustc-LLVM ERROR: IO failure on output stream: No space left on device
error: could not compile ...
```

**Fix:** free at least **5–10 GB**, then retry:

```bash
# Check free space (look at "Avail" on /System/Volumes/Data)
df -h /System/Volumes/Data

# Safe dev cache cleanup
pip3 cache purge
npm cache clean --force
brew cleanup -s          # if you use Homebrew
rm -rf src-tauri/target  # old Rust build artifacts in this repo

# Retry
npm run tauri:dev
```

Also empty **Trash** and check **System Settings → General → Storage** for large files.

### `cargo: command not found` or `tauri: command not found`

Install Rust and project deps:

```bash
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
source "$HOME/.cargo/env"
npm install
cd frontend && npm install && cd ..
```

### First run is slow

The first `tauri dev` compiles all Rust dependencies and can take **5–15 minutes**. Later runs are much faster.

### App opens but capture stays idle

Grant macOS **Accessibility** and **Input Monitoring** permissions, restart the app, then use **Refresh permissions** in the dashboard.

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
