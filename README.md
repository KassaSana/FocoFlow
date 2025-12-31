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

# Start backend
cd ../backend
./mvnw spring-boot:run

# Start frontend
cd ../frontend
npm install && npm run dev
```

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

## Contributing

This is currently a personal learning project, but contributions are welcome once the MVP is complete.

## Acknowledgments

- Inspired by the need for better ADHD productivity tools
- Built to learn production ML systems engineering

---

**Status:** Currently building. Currently building Phase 1 (C++ event engine).


**Last Updated:** December 29, 2025

