# FocoFlow: Progress

## 2026-06 — Tauri overhaul (v0.2)

- Collapsed 4-layer architecture into **one Tauri desktop app** (`src-tauri/` + `frontend/`).
- **Removed:** Java/Spring Boot backend, C++/ZeroMQ capture stack, Docker compose demo, inference bridge service.
- **Rust core:** cross-platform capture (`rdev` + active window), rolling-window features, heuristic classifier, SQLite, snapback state machine + overlay window.
- **React UI:** Tauri `invoke`/`listen` instead of REST/WebSocket; focus modes, feedback labels, session recap.
- **Python:** kept as offline training tool; added `ml/export_onnx.py`.
- **CI:** dropped Java job; added `cargo check` + `cargo test`.

See [docs/archive/PROGRESS_full.md](docs/archive/PROGRESS_full.md) for the earlier session journal.
