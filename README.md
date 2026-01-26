# Neural Focus

**Predictive Focus Management System with ML-Powered Interventions**

![Status](https://img.shields.io/badge/status-in%20development-yellow)
![Platform](https://img.shields.io/badge/platform-Windows-blue)
![License](https://img.shields.io/badge/license-MIT-green)

## What is Neural Focus?

Neural Focus is an ML-powered productivity system that **predicts** when you're about to lose focus and **intervenes proactively** before distractions happen.

Traditional productivity trackers are reactive—they tell you what happened *after* you've lost focus. Neural Focus is **predictive**—it learns your behavioral patterns and blocks distractions 30 seconds before you'd naturally context-switch.

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
NeuralFocus/
├── core/                    # C++ event capture engine
│   ├── event_processor.cpp  # Lock-free ring buffer
│   ├── windows_hooks.cpp    # Win32 API integration
│   ├── feature_engine.cpp   # SIMD-optimized feature extraction
│   └── mmap_logger.cpp      # Memory-mapped file logging
│
├── ml/                      # Python ML pipeline
│   ├── data_loader.py       # Event log parser
│   ├── feature_engineering.py  # Rolling window features
│   ├── models.py            # XGBoost + LSTM implementations
│   ├── training_pipeline.py    # Model training orchestration
│   └── inference_server.py     # Real-time prediction service
│
├── backend/                 # Spring Boot API
│   └── src/main/java/com/neurofocus/
│       ├── controllers/     # REST endpoints
│       ├── services/        # Business logic
│       └── repositories/    # Database access
│
├── frontend/                # React TypeScript UI
│   └── src/
│       ├── components/      # Dashboard, charts, controls
│       ├── stores/          # Zustand state management
│       └── services/        # WebSocket + REST clients
│
├── docs/                    # Documentation
│   ├── TECHNICAL_DESIGN_DOCUMENT.md  # System design (this was just created)
│   ├── ARCHITECTURE.md      # Component diagrams
│   └── API_SPEC.md          # REST/WebSocket contracts
│
└── tools/                   # Development utilities
    ├── labeler.py           # Ground truth annotation UI
    └── stress_test.py       # Performance benchmarking
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
git clone https://github.com/yourusername/neural-focus.git
cd neural-focus

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
.\tools\start_neural_focus.ps1 -LogPath .\events_test_2026-01-02.log -ReplaySleepMs 5

# Run just the inference bridge (manual)
python -m ml.inference_server --backend-url http://localhost:8080 --session-goal "Deep work demo"

# Replay a recorded log into ZeroMQ (for demos)
python -m ml.event_replay --log-path .\events_test_2026-01-02.log --endpoint tcp://127.0.0.1:5560
```

### Demo flow (log replay)

1. Start the backend:
   - `mvn spring-boot:run` (in `backend`)
   - Or: `docker compose up -d backend`
2. Run the stack with log replay:
   - `.\tools\start_neural_focus.ps1 -SessionGoal "Deep work demo" -LogPath .\events_test_2026-01-02.log -ReplaySleepMs 5`
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
python -m ml.metrics_report --log-path .\events_test_2026-01-02.log --benchmark-features
```

See `docs/METRICS.md` for a sample report.
See `docs/DEPLOYMENT.md` for Docker-based deployment instructions.

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
    *   If `events_test_2026-01-02.log` is missing, generate a dummy one:
        ```bash
        python generate_log.py
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

## License

MIT License - See [LICENSE](LICENSE) for details

## Acknowledgments

- Inspired by the need for better ADHD productivity tools
- Built to learn production ML systems engineering
- Designed to be a standout project for alignment fellowships & big tech interviews

---

**Status:** In active development. Currently building Phase 1 (C++ event engine).

**Author:** [Your Name]  
**Contact:** [Your Email]  
**Last Updated:** December 29, 2025
