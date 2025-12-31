# Neural Focus: System Architecture

This document provides visual diagrams and component-level explanations of the Neural Focus architecture.

## Table of Contents
1. [High-Level System Overview](#high-level-system-overview)
2. [Component Interaction Flow](#component-interaction-flow)
3. [Data Flow Diagrams](#data-flow-diagrams)
4. [State Machines](#state-machines)
5. [Deployment Architecture](#deployment-architecture)

---

## High-Level System Overview

Neural Focus follows a **layered architecture** with strict separation of concerns:

```
┌─────────────────────────────────────────────────────────────────────┐
│                         User's Desktop                              │
│  ┌────────────┐  ┌────────────┐  ┌────────────┐  ┌────────────┐  │
│  │  Browser   │  │    IDE     │  │   Slack    │  │  Spotify   │  │
│  └─────┬──────┘  └─────┬──────┘  └─────┬──────┘  └─────┬──────┘  │
│        │               │               │               │           │
│        └───────────────┴───────────────┴───────────────┘           │
│                        │                                            │
│                        ▼                                            │
│  ┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓  │
│  ┃           Layer 1: C++ Event Capture Engine                 ┃  │
│  ┃  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐      ┃  │
│  ┃  │ Windows Hooks│  │ Ring Buffer  │  │ MMap Logger  │      ┃  │
│  ┃  │   (Win32)    │─▶│ (lock-free)  │─▶│  (events)    │      ┃  │
│  ┃  └──────────────┘  └──────────────┘  └──────────────┘      ┃  │
│  ┃              ↓ IPC (ZeroMQ PUB/SUB)                          ┃  │
│  ┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛  │
└───────────────────────────────┼──────────────────────────────────────┘
                                │
                                ▼
  ┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓
  ┃           Layer 2: Python ML Inference Service              ┃
  ┃  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐      ┃
  ┃  │ Event Subscriber│Feature Engine│  │ LSTM Model   │      ┃
  ┃  │   (ZeroMQ)    │─▶│ (NumPy/SIMD) │─▶│(TorchScript) │      ┃
  ┃  └──────────────┘  └──────────────┘  └──────────────┘      ┃
  ┃              ↓ WebSocket (prediction stream)                 ┃
  ┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛
                                │
                                ▼
  ┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓
  ┃           Layer 3: Spring Boot Backend API                  ┃
  ┃  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐      ┃
  ┃  │ REST Endpoints│ │ WebSocket Hub│  │ PostgreSQL   │      ┃
  ┃  │ (sessions)   │─┼▶│ (live data)  │  │ (TimescaleDB)│      ┃
  ┃  └──────────────┘  └──────────────┘  └──────────────┘      ┃
  ┃              ↓ REST + WebSocket (wss://localhost:8080)      ┃
  ┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛
                                │
                                ▼
  ┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓
  ┃           Layer 4: React TypeScript Frontend                ┃
  ┃  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐      ┃
  ┃  │ Focus Control│  │ Live Charts  │  │  Analytics   │      ┃
  ┃  │ (timers/jail)│  │ (predictions)│  │ (weekly data)│      ┃
  ┃  └──────────────┘  └──────────────┘  └──────────────┘      ┃
  ┃              ↑ Zustand Store (global state)                 ┃
  ┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛
```

### Why This Layering?

Each layer has a specific **responsibility** and **performance characteristic**:

| Layer | Language | Latency | Why |
|-------|----------|---------|-----|
| 1 (Capture) | C++ | <1ms | OS integration, performance critical |
| 2 (ML) | Python | 10ms | ML ecosystem, NumPy/PyTorch |
| 3 (Backend) | Java | 50ms | Enterprise patterns, PostgreSQL |
| 4 (Frontend) | TypeScript | 100ms | User interaction, not latency-critical |

**Key Insight:** We isolate the 1ms-critical code (C++) from the 100ms-tolerant code (React). This means:
- Frontend redesigns don't touch the event engine
- Model retraining doesn't require C++ changes
- Each layer evolves independently

---

## Component Interaction Flow

### Scenario: User Types in VS Code

Let's trace what happens when you type a single keystroke:

```
Time    Component               Action                          State Change
────────────────────────────────────────────────────────────────────────────
t=0     OS (Windows)           User presses 'a' key            Interrupt fired
        
t=0.1ms Win32 Hook (C++)       Callback invoked                hook_state = ACTIVE
                               Extract: timestamp, key_event
                               
t=0.3ms Event Classifier       Determine: KEY_PRESS            event.type = KEY_PRESS
        (C++)                  Current app: "Code.exe"         event.app = "Code.exe"
                               
t=0.5ms Ring Buffer            Try push to buffer              buffer_head++
        (C++)                  Success? Yes (not full)         buffered_count = 1234
                               
t=0.6ms MMap Logger            Write to memory-mapped file     events.log += 64 bytes
        (C++)                  No syscall needed               
                               
t=1ms   ZeroMQ Publisher       Send event to Python            zmq_pub.send(event)
        (C++)                  Non-blocking (dontwait)         
        
        ─────────────── IPC Boundary ───────────────
        
t=1.1ms ZeroMQ Subscriber      Receive event                   subscriber.recv()
        (Python)               
                               
t=2ms   Feature Engine         Update rolling window:          window[30s].append(event)
        (Python)               - Last 30s: 150 keystrokes      
                               - Inter-keystroke: 0.2s avg     
                               
t=3ms   Inference Queue        Check: Is it time to predict?   time_since_last = 9.8s
        (Python)               Yes (every 10s batch)           
                               
t=4ms   LSTM Model             Forward pass:                   model.forward(features)
        (TorchScript)          Input: [150 keystrokes, ...]    
                               Hidden state: [h_t, c_t]        
                               Output: focus_score = 72        
                               
t=6ms   Intervention Logic     Evaluate risk:                  risk = 0.25 (LOW)
        (Python)               risk < 0.7 → No action          intervention = None
        
t=8ms   WebSocket Client       Send prediction to backend      ws.send({score: 72})
        (Python)               
        
        ─────────────── Network Boundary ───────────────
        
t=10ms  Spring Boot            Receive WebSocket message       @MessageMapping
        (Java)                 Broadcast to frontend           
                               
t=15ms  React Frontend         Zustand store updated           focusScore = 72
        (TypeScript)           Gauge re-renders                UI updates
        
t=20ms  User                   Sees updated focus score        Visual feedback
```

**Total Latency:** 20ms (user types → UI updates)

**Educational Note: Why This Is Fast**

1. **Lock-free ring buffer** (t=0.5ms): No mutex contention, just atomic pointer increment
2. **Memory-mapped logging** (t=0.6ms): Direct memory write, no `fwrite()` syscall overhead
3. **Batch inference** (t=3ms): Only run LSTM every 10s, not on every keystroke
4. **TorchScript** (t=4ms): Compiled model, 10x faster than Python eager mode
5. **WebSocket** (t=8ms): Persistent connection, no HTTP handshake overhead

### What If Performance Degrades?

**Problem:** LSTM takes 50ms instead of 4ms (maybe CPU is thermal throttling).

**System Behavior:**
- Layer 1 (C++) continues capturing at <1ms (unaffected)
- Layer 2 (Python) prediction lags, queue builds up
- Layer 3/4 (Backend/Frontend) show stale focus score

**Back-Pressure Handling:**
```python
# Python inference service
if inference_queue.size() > 100:
    # Skip this batch, we're falling behind
    logger.warning("Inference queue full, dropping batch")
    inference_queue.clear()  # Catch up
```

**Recovery:** System automatically recovers when CPU is available again. No crashes, no blocking.

---

## Data Flow Diagrams

### Data Flow 1: Event Capture (Write Path)

```
┌─────────────┐
│ User Action │ (typing, clicking, switching apps)
└──────┬──────┘
       │
       ▼
┌─────────────────────────────────────────────────┐
│         Win32 Event Hooks (C++)                 │
│  SetWindowsHookEx(WH_KEYBOARD_LL, callback)     │
│  SetWinEventHook(EVENT_SYSTEM_FOREGROUND, ...)  │
└──────┬──────────────────────────────────────────┘
       │ Capture: timestamp, type, app, window
       ▼
┌─────────────────────────────────────────────────┐
│         Event Classifier (C++)                  │
│  - Filter noise (system events, background)     │
│  - Categorize: KEY_PRESS, MOUSE_MOVE, etc.      │
│  - Enrich: Add process name, window title       │
└──────┬──────────────────────────────────────────┘
       │ Structured Event (64 bytes)
       ▼
┌─────────────────────────────────────────────────┐
│         Lock-Free Ring Buffer                   │
│  Capacity: 65,536 events (1.3s @ 50k/sec)       │
│  Pattern: Single-Producer-Single-Consumer       │
│  Overflow: Drop oldest events                   │
└──────┬──────────────────────────────────────────┘
       │
       ├──▶ Memory-Mapped File (persistence)
       │    events_2025-12-29.log
       │
       └──▶ ZeroMQ Publisher (IPC)
            ipc:///tmp/neurofocus_events
```

**Key Design Decision: Why Memory-Mapped File?**

Traditional file I/O:
```cpp
FILE* f = fopen("events.log", "a");
fwrite(&event, sizeof(Event), 1, f);  // Copies to kernel buffer (syscall)
fflush(f);                             // Forces kernel to disk (syscall)
```
**Latency:** 50-100μs (2 syscalls)

Memory-mapped I/O:
```cpp
void* log_base = mmap(...);
memcpy(log_base + offset, &event, sizeof(Event));  // Direct memory write
// OS flushes to disk in background
```
**Latency:** 5-10μs (0 syscalls, just memory copy)

**Trade-off:** If system crashes before OS flushes, last few seconds of events are lost. **Acceptable** because we're not a database (no financial transactions); we can tolerate losing 1-2s of data.

---

### Data Flow 2: ML Inference (Read Path)

```
┌─────────────────────────────────────────────────┐
│   ZeroMQ Subscriber (Python)                    │
│   socket.recv() → 64-byte Event                 │
└──────┬──────────────────────────────────────────┘
       │
       ▼
┌─────────────────────────────────────────────────┐
│   Time-Windowed Buffer (Python)                 │
│   - Rolling window: Last 30 seconds             │
│   - Stores: events[t-30s : t]                   │
│   - Evict: Drop events older than 30s           │
└──────┬──────────────────────────────────────────┘
       │ Every 10 seconds (batch trigger)
       ▼
┌─────────────────────────────────────────────────┐
│   Feature Engineering Pipeline                  │
│   ┌───────────────────────────────────────┐    │
│   │ Extract:                               │    │
│   │ - Keystroke rate (events/sec)          │    │
│   │ - Keystroke variance (consistency)     │    │
│   │ - Mouse speed (pixels/sec)             │    │
│   │ - Mouse jitter (stddev of velocity)    │    │
│   │ - Context switches (app changes)       │    │
│   │ - Idle periods (gaps >5s)              │    │
│   │ - Time features (hour, session_mins)   │    │
│   └───────────────────────────────────────┘    │
└──────┬──────────────────────────────────────────┘
       │ Feature Vector (20 floats)
       ▼
┌─────────────────────────────────────────────────┐
│   LSTM Model (TorchScript)                      │
│   Input: Sequence of 30 feature vectors         │
│   Hidden State: (h_t, c_t) from last step       │
│   Output: [focus_score, risk_score]             │
└──────┬──────────────────────────────────────────┘
       │ Prediction
       ▼
┌─────────────────────────────────────────────────┐
│   Intervention Decision Engine                  │
│   if risk > 0.7:                                │
│       if in_browser:                            │
│           → Block distracting sites             │
│       if in_comm_app:                           │
│           → Suggest "Close Slack?"              │
│   else:                                         │
│       → No action                               │
└──────┬──────────────────────────────────────────┘
       │ Action (if any)
       ▼
┌─────────────────────────────────────────────────┐
│   WebSocket Publisher → Spring Boot → React     │
│   { type: "PREDICTION", score: 72, risk: 0.3 }  │
└─────────────────────────────────────────────────┘
```

**Educational Note: Feature Engineering is Critical**

The model is only as good as the features. Example:

**Bad features:**
```python
features = {
    "total_keystrokes": 1250,  # Raw count, no context
    "total_time": 3600         # 1 hour, doesn't tell us focus state
}
```
Model can't learn anything useful. "1250 keystrokes in 1 hour" could be focused coding OR frequent context switching.

**Good features:**
```python
features = {
    "keystroke_rate_30s": 4.2,        # Recent activity (not lifetime)
    "keystroke_variance_30s": 0.15,   # Consistency (low = steady typing)
    "context_switches_30s": 1,        # Stability (low = focused)
    "typing_speed_trend": -0.02       # Slowing down? (fatigue indicator)
}
```
Model learns: "High rate + low variance + few switches + stable trend = FOCUSED"

**How do we discover good features?**  
**Exploratory Data Analysis (EDA)** - plot your own data:

```python
import pandas as pd
import matplotlib.pyplot as plt

# Load your collected data
sessions = pd.read_csv("sessions.csv")

# Hypothesis: Typing speed decreases before distractions
focused = sessions[sessions['label'] == 'FOCUSED']
distracted = sessions[sessions['label'] == 'DISTRACTED']

plt.hist(focused['keystroke_rate'], alpha=0.5, label='Focused')
plt.hist(distracted['keystroke_rate'], alpha=0.5, label='Distracted')
plt.legend()
plt.show()

# If histograms are separated → keystroke_rate is a good feature!
# If they overlap → this feature doesn't discriminate, try another
```

---

## State Machines

### State Machine 1: Focus Session Lifecycle

```
                 ┌──────────────┐
                 │              │
          ┌─────▶│     IDLE     │◀─────┐
          │      │              │      │
          │      └──────┬───────┘      │
          │             │              │
          │     User clicks "Start"    │
          │             │              │
          │             ▼              │
          │      ┌──────────────┐     │
          │      │              │     │
          │      │  ACTIVE      │     │ Session ends (timer expires)
          │      │  (counting)  │     │
          │      │              │     │
          │      └──────┬───────┘     │
          │             │              │
          │   User disables jail /    │
          │   Closes app              │
          │             │              │
          │             ▼              │
          │      ┌──────────────┐     │
          │      │              │     │
          └──────│  COMPLETED   │─────┘
                 │              │
                 └──────────────┘
                 
States:
- IDLE: No focus session active
- ACTIVE: Timer running, events being logged
- COMPLETED: Session ended, stats calculated

Transitions:
- IDLE → ACTIVE: User starts Pomodoro (25min) or Deep Work (90min)
- ACTIVE → COMPLETED: Timer expires OR user manually ends
- COMPLETED → IDLE: After stats saved to database

State Storage:
class FocusSession {
    state: "IDLE" | "ACTIVE" | "COMPLETED";
    start_time: DateTime;
    duration_mins: number;
    jail_enabled: boolean;
    context_switches: number;
}
```

**Educational Note: Why State Machines?**

State machines make complex behavior **predictable** and **testable**.

Without state machine:
```python
def on_user_clicks_start():
    if not currently_running:
        start_timer()
        if user_selected_deep_work:
            enable_jail()
    # What if currently_running is True? Undefined!
```

With state machine:
```python
def on_user_clicks_start():
    if state == IDLE:
        state = ACTIVE
        start_timer()
        if session_type == "deep_work":
            enable_jail()
    else:
        raise InvalidStateError("Cannot start, already active")
```

**Benefits:**
1. **Predictable:** Every state has defined transitions
2. **Testable:** Test each transition independently
3. **Debuggable:** Log state changes to trace bugs

---

### State Machine 2: Intervention Lifecycle

```
         ┌──────────────┐
         │              │
         │  MONITORING  │───────────┐
         │              │           │
         └──────┬───────┘           │
                │                   │
      Risk score > 0.7              │ Risk < 0.7 (no action)
                │                   │
                ▼                   │
         ┌──────────────┐           │
         │              │           │
         │   PENDING    │───────────┤
         │  (notified)  │           │
         │              │           │
         └──────┬───────┘           │
                │                   │
         User responds              │
                │                   │
      ┌─────────┴─────────┐         │
      │                   │         │
      ▼                   ▼         │
┌─────────────┐   ┌─────────────┐  │
│             │   │             │  │
│  ACCEPTED   │   │  REJECTED   │  │
│ (blocking)  │   │  (ignored)  │  │
│             │   │             │  │
└─────┬───────┘   └─────┬───────┘  │
      │                 │           │
      └─────────┬───────┘           │
                │                   │
                ▼                   │
         ┌──────────────┐           │
         │              │           │
         │  COMPLETED   │───────────┘
         │  (logged)    │
         │              │
         └──────────────┘

States:
- MONITORING: Continuously evaluating risk
- PENDING: Intervention triggered, waiting for user response
- ACCEPTED: User approved, blocks activated
- REJECTED: User declined, no action taken
- COMPLETED: Outcome logged for model retraining

Transitions:
- MONITORING → PENDING: risk_score > threshold (0.7)
- PENDING → ACCEPTED: user clicks "Block now" (timeout: 10s)
- PENDING → REJECTED: user clicks "Ignore" OR no response
- ACCEPTED/REJECTED → COMPLETED: Action executed, log to database
- COMPLETED → MONITORING: Ready for next prediction cycle
```

**Educational Note: Why Track Intervention Outcomes?**

We log every intervention (accepted/rejected) to create a **feedback loop** for model improvement.

```sql
-- interventions table
INSERT INTO interventions (
    session_id, 
    triggered_at, 
    predicted_risk, 
    user_feedback,
    effectiveness_score
) VALUES (
    123,
    '2025-12-29 14:32:15',
    0.85,                      -- High risk prediction
    'ACCEPTED',                -- User agreed to block
    0.90                       -- Did focus improve after? (calculated post-hoc)
);
```

**Later, we retrain the model:**
```python
# Load intervention feedback
feedback = load_interventions()

# High-risk predictions that were ACCEPTED and EFFECTIVE → Reinforce
# High-risk predictions that were REJECTED → Model was too aggressive
# Low-risk predictions where user switched anyway → Model missed it

# Re-weight training samples based on feedback
sample_weights = compute_weights(feedback)
model.fit(X, y, sample_weight=sample_weights)
```

This is **Reinforcement Learning from Human Feedback (RLHF)** - the model learns what predictions are actually useful to you.

---

## Deployment Architecture

### Phase 1: Local-Only (MVP)

```
┌────────────────────────────────────────────────────────────┐
│             User's Windows PC (localhost)                  │
│                                                            │
│  ┌──────────────────────────────────────────────────────┐ │
│  │ C++ Event Engine (native executable)                 │ │
│  │ Process: neurofocus-engine.exe                       │ │
│  │ Port: N/A (IPC only)                                 │ │
│  └────────────┬─────────────────────────────────────────┘ │
│               │ IPC: ipc:///tmp/neurofocus_events         │
│               ▼                                            │
│  ┌──────────────────────────────────────────────────────┐ │
│  │ Python ML Service                                    │ │
│  │ Process: python ml/inference_server.py               │ │
│  │ Port: 5000 (WebSocket)                               │ │
│  └────────────┬─────────────────────────────────────────┘ │
│               │ WebSocket: ws://localhost:5000            │
│               ▼                                            │
│  ┌──────────────────────────────────────────────────────┐ │
│  │ Spring Boot Backend                                  │ │
│  │ Process: java -jar backend.jar                       │ │
│  │ Port: 8080 (REST + WebSocket)                        │ │
│  │ Database: PostgreSQL @ localhost:5432                │ │
│  └────────────┬─────────────────────────────────────────┘ │
│               │ HTTP: http://localhost:8080               │
│               ▼                                            │
│  ┌──────────────────────────────────────────────────────┐ │
│  │ React Frontend                                       │ │
│  │ Process: npm run dev (Vite dev server)               │ │
│  │ Port: 3000 (HTTP)                                    │ │
│  │ URL: http://localhost:3000                           │ │
│  └──────────────────────────────────────────────────────┘ │
│                                                            │
│  User opens browser → http://localhost:3000                │
└────────────────────────────────────────────────────────────┘
```

**Startup Sequence:**
1. User runs `start_neural_focus.bat`:
   ```batch
   @echo off
   start neurofocus-engine.exe
   timeout /t 2
   start python ml/inference_server.py
   timeout /t 3
   start java -jar backend.jar
   timeout /t 5
   start chrome http://localhost:3000
   ```

2. Services start in order (C++ → Python → Java → React)
3. Health checks ensure each layer is ready before starting next

**Educational Note: Why This Startup Order?**

- **C++ first:** Starts capturing events immediately (don't miss data)
- **Python second:** Needs events from C++ to be flowing
- **Java third:** Needs Python predictions to proxy
- **React last:** Just a UI, least critical

If we started in wrong order (e.g., Python before C++), Python would crash trying to connect to non-existent IPC socket.

---

## Summary: Key Architectural Patterns

1. **Layered Architecture:** Strict separation (C++ ≠ Python ≠ Java ≠ React)
2. **Event-Driven:** Pub/Sub between layers (loose coupling)
3. **CQRS:** Separate write path (C++) from read path (Python)
4. **State Machines:** Explicit states (session, intervention)
5. **Lock-Free Concurrency:** Ring buffer (no mutex contention)
6. **Memory-Mapped I/O:** Zero-copy logging (performance)
7. **Batch + Stream Hybrid:** Batch inference (efficiency) + immediate triggers (responsiveness)
8. **Back-Pressure Handling:** Drop events rather than block (graceful degradation)
9. **Feedback Loops:** Log interventions for model retraining (RLHF)
10. **Local-First:** All data on-device (privacy)

Each pattern solves a specific problem. As we implement, you'll see *why* each decision matters.

---

**Next:** Let's start building the C++ event engine (Phase 1, Week 1).
