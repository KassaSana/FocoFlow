# FocoFlow

**Predictive Focus Management System with ML-Powered Interventions**

![Status](https://img.shields.io/badge/status-in%20development-yellow)
![Platform](https://img.shields.io/badge/platform-Windows-blue)
![License](https://img.shields.io/badge/license-MIT-green)

## What is FocoFlow?

FocoFlow is an ML-powered productivity system that **predicts** when you're about to lose focus and **intervenes proactively** before distractions happen.

Traditional productivity trackers are reactive—they tell you what happened *after* you've lost focus. FocoFlow is **predictive**—it learns your behavioral patterns and blocks distractions 30 seconds before you'd naturally context-switch.

### Key Features

- **Sub-millisecond event capture** - C++ engine processes 10,000+ events/second
- **Real-time ML predictions** - LSTM model forecasts focus degradation
- **Adaptive interventions** - Proactive blocking based on your patterns
- **Privacy-first** - All data stays local, no cloud transmission
- **Full-stack dashboard** - Real-time focus score, predictions, analytics

## Architecture

```
C++ Event Engine (Win32) → Python ML Service → Spring Boot API → React Dashboard
    <1ms latency           10ms inference      REST + WebSocket    Real-time UI
```

See [Technical Design Document](docs/TECHNICAL_DESIGN_DOCUMENT.md) for complete system architecture.

## Project Structure

```
FocoFlow/
├── core/                       # C++ event capture engine (Windows-only)
│   ├── event.h                 # Event struct (cache-aligned)
│   ├── ring_buffer.h           # Lock-free SPSC queue
│   ├── windows_hooks.{h,cpp}   # Win32 low-level hook capture
│   ├── event_processor.{h,cpp} # Hooks -> ring buffer coordinator
│   ├── mmap_logger.{h,cpp}     # Memory-mapped event log
│   ├── zmq_publisher.{h,cpp}   # ZeroMQ event publisher
│   ├── neurofocus_engine.cpp   # End-to-end runner (capture + log + publish)
│   ├── experimental/           # Unwired context-recovery R&D headers + demo
│   └── CMakeLists.txt
│
├── ml/                         # Python ML pipeline
│   ├── event_schema.py         # Shared binary struct definitions
│   ├── event_log_reader.py     # Parse the C++ binary log
│   ├── event_replay.py         # Replay a log into ZeroMQ
│   ├── features.py             # Rolling-window feature extraction
│   ├── dataset_builder.py      # Build training datasets from logs
│   ├── labeling.py             # Label assignment workflow
│   ├── training_pipeline.py    # Train baseline classifier
│   ├── train_cli.py            # CLI entry point for training
│   ├── inference_server.py     # ZMQ subscriber -> REST/WS bridge
│   ├── zmq_subscriber.py       # ZeroMQ event subscriber
│   └── metrics_report.py       # Summarize logs, benchmark features
│
├── backend/                    # Spring Boot REST + WebSocket API
│   └── src/main/
│       ├── java/com/neurofocus/
│       │   ├── NeuroFocusApplication.java   # Spring Boot entry
│       │   ├── config/                      # CorsConfig
│       │   ├── controllers/                 # /api/health, /api/predictions, /api/sessions
│       │   ├── predictions/                 # Prediction model + service
│       │   ├── sessions/                    # Session model + service
│       │   └── websocket/                   # /ws/predictions stream
│       └── resources/application.properties
│
├── frontend/                   # React + Vite + TypeScript dashboard
│   ├── src/
│   │   ├── App.tsx             # Main dashboard + WS client
│   │   ├── main.tsx            # React entry point
│   │   ├── utils.ts            # Formatting + risk helpers
│   │   └── styles.css
│   ├── index.html
│   └── package.json
│
├── docs/                       # Design + reference documentation
│   ├── TECHNICAL_DESIGN_DOCUMENT.md
│   ├── ARCHITECTURE.md
│   ├── CONCEPTS.md
│   ├── SCHEMAS.md
│   ├── CONTEXT_RECOVERY_DESIGN.md
│   ├── METRICS.md
│   └── DEPLOYMENT.md
│
├── tools/                      # Developer scripts
│   ├── generate_log.py         # Synthesize a demo event log
│   ├── start_neural_focus.{ps1,py}  # Launch backend + ML + frontend
│   ├── build_core.ps1          # Configure + build the C++ engine
│   └── benchmark.cpp           # Core benchmark
│
├── samples/
│   └── events_demo.bin         # Cold-clone demo log (committed)
│
├── docker-compose.yml
├── docker-compose.demo.yml
├── README.md
└── PROGRESS.md
```

## Quick Start

### Prerequisites

- **C++ Compiler:** MSVC 2022 or GCC 11+
- **Python:** 3.10+
- **Java:** JDK 17+
- **Node.js:** 18+
- **PostgreSQL:** 14+

### Setup (Coming Soon)

```bash
# Clone repository
git clone https://github.com/KassaSana/FocoFlow.git
cd FocoFlow

# Build C++ engine
cd core && mkdir build && cd build
cmake .. && make

# Set up Python environment
cd ../../ml
python -m venv venv
source venv/bin/activate  # or venv\Scripts\activate on Windows
pip install -r requirements.txt
# Optional: install xgboost to enable the XGBoost training backend
pip install xgboost

# Start backend
cd ../backend
chmod +x mvnw  # macOS/Linux only (first time)
./mvnw spring-boot:run  # or mvnw.cmd spring-boot:run on Windows

# Start frontend
cd ../frontend
npm install && npm run dev
```

### Local demo helpers

```powershell
# Start backend + inference bridge + frontend (PowerShell)
.\tools\start_neural_focus.ps1

# Cross-platform launcher (Python)
python .\tools\start_neural_focus.py

# Build the core engine (PowerShell)
.\tools\build_core.ps1

# Start the C++ engine (requires ZeroMQ headers/libs and a built exe)
.\core\build\bin\neurofocus_engine.exe --log-dir . --log-base events

# Start stack + replay a recorded log into ZeroMQ (PowerShell)
.\tools\start_neural_focus.ps1 -LogPath .\samples/events_demo.bin -ReplaySleepMs 5

# Run just the inference bridge (manual)
python -m ml.inference_server --backend-url http://localhost:8080 --session-goal "Deep work demo"

# Replay a recorded log into ZeroMQ (for demos)
python -m ml.event_replay --log-path .\samples/events_demo.bin --endpoint tcp://127.0.0.1:5560
```

### Demo flow (log replay)

1. Start the backend:
   - `mvn spring-boot:run` (in `backend`)
   - Or: `docker compose up -d backend`
2. Run the stack with log replay:
   - `.\tools\start_neural_focus.ps1 -SessionGoal "Deep work demo" -LogPath .\samples/events_demo.bin -ReplaySleepMs 5`
3. Open the UI at `http://localhost:5173` and confirm the Live Prediction card updates and the history list fills.

### Docker demo (one command)

```powershell
docker compose -f docker-compose.yml -f docker-compose.demo.yml up -d --build
```

This starts the backend, frontend, inference bridge, and replay publisher together.

### Docker backend (optional)

```powershell
# Start the Spring Boot API without local Maven
docker compose up -d backend
```

### Metrics & Benchmarks

```powershell
# Summarize a recorded log and feature throughput
python -m ml.metrics_report --log-path .\samples/events_demo.bin --benchmark-features
```

See `docs/METRICS.md` for a sample report.
See `docs/DEPLOYMENT.md` for Docker-based deployment instructions.

## Documentation

- [Technical Design Document](docs/TECHNICAL_DESIGN_DOCUMENT.md) — full system design and rationale
- [Architecture](docs/ARCHITECTURE.md) — diagrams and layered component breakdown
- [Concepts](docs/CONCEPTS.md) — CS concepts used in the project (lock-free, mmap, SIMD, LSTM, etc.)
- [Schemas](docs/SCHEMAS.md) — binary event format, feature vectors, REST/WS payloads
- [Context Recovery Design](docs/CONTEXT_RECOVERY_DESIGN.md) — design notes for the experimental "Where was I?" feature
- [Metrics](docs/METRICS.md) — measured numbers and what they mean
- [Deployment](docs/DEPLOYMENT.md) — Docker-based deployment instructions

## Troubleshooting

### Common Issues

1.  **Backend fails to start:**
    *   Ensure Java 17+ is installed (`java -version`).
    *   Ensure Maven is installed (`mvn -version`) or use the Docker method.
    *   If using Docker, ensure Docker Desktop is running (`docker info`).

2.  **Port Conflicts:**
    *   **8080 (Backend):** If occupied, change `server.port` in `backend/src/main/resources/application.properties`.
    *   **5173 (Frontend):** Vite will automatically switch to 5174+ if 5173 is busy.
    *   **5560 (ZeroMQ):** Ensure no other instance of the engine or replay script is running.

3.  **Missing Log File:**
    *   If `samples/events_demo.bin` is missing, regenerate it:
        ```bash
        python tools/generate_log.py
        ```

4.  **UI Not Updating:**
    *   Check if the backend is running (`http://localhost:8080/actuator/health`).
    *   Check if the inference server is running (check terminal logs).
    *   Ensure the replay script is actually sending events (check for "Published event" logs).

## Development Roadmap

- [x] **Phase 0:** Technical design document (December 2025)
- [ ] **Phase 1:** C++ event engine + data collection (January 2026)
- [ ] **Phase 2:** ML training pipeline + real-time inference (February 2026)
- [ ] **Phase 3:** Full-stack UI + interventions (March 2026)
- [ ] **Phase 4:** Federated learning (April 2026)

## Learning Resources

This project is designed to teach:
- **Systems Programming:** Lock-free data structures, memory-mapped I/O, SIMD
- **Machine Learning:** Time-series prediction, LSTM, feature engineering
- **Backend Engineering:** REST APIs, WebSockets, database design
- **Frontend Development:** React, real-time state management, charting
- **DevOps:** Performance profiling, latency optimization, A/B testing

Each component includes detailed educational comments explaining design decisions and patterns.

## Performance Targets

| Metric | Target | Why It Matters |
|--------|--------|----------------|
| Event capture latency | <1ms p99 | No user-perceivable lag |
| Prediction latency | <10ms | Real-time interventions |
| Throughput | 10k events/sec | Handle rapid typing + mouse |
| Prediction accuracy | >75% precision | Avoid false alarms |
| CPU overhead | <5% avg | Runs in background |

## CI

Automated checks run via `.github/workflows/ci.yml` (Python, frontend, backend tests).

## Contributing

This is currently a personal learning project, but contributions are welcome once the MVP is complete.

## Acknowledgments

- Inspired by the need for better ADHD productivity tools
- Built to learn production ML systems engineering

---

**Status:** Engine, ML pipeline, backend, and frontend all have runnable code. The C++ capture engine is Windows-only; on other platforms run the cold-clone demo (`samples/events_demo.bin` + `ml.event_replay`).


**Last Updated:** April 25, 2026

