# Neural Focus: Technical Design Document

**Document Status:** Draft v1.0  
**Authors:** Development Team  
**Last Updated:** December 29, 2025  
**Review Status:** Pending Architecture Review

---

## Executive Summary

### Problem Statement
Traditional productivity trackers are **reactive** - they tell you *what happened* after you've already lost focus. Users with ADHD need **predictive intervention** - systems that detect focus degradation patterns and intervene *before* context switches occur.

Current desktop monitoring tools suffer from:
- **High latency** (50-100ms event processing) that misses rapid context switches
- **No predictive capability** - pure logging without behavioral modeling
- **Coarse granularity** - only tracking application switches, missing micro-behaviors (typing cadence, mouse jitter)
- **Manual intervention** - users must consciously activate blocking, which fails when impulsivity strikes

### Proposed Solution
**Neural Focus** is a multi-tier ML-powered productivity system that:
1. **Captures** every system event (window changes, keystrokes, mouse movements) with <1ms latency using a C++ event engine
2. **Predicts** distraction risk 30-60 seconds ahead using LSTM models trained on user behavioral patterns
3. **Intervenes** proactively with adaptive blocking (hosts file modification, process termination) based on predicted focus state
4. **Learns** continuously from user feedback on intervention effectiveness

**Key Innovation:** Sub-second prediction pipeline (event → classification → feature extraction → ML inference → intervention decision) enabling preemptive focus protection rather than reactive blocking.

### Success Metrics
- **Latency:** <1ms p99 for event capture, <10ms end-to-end for prediction
- **Throughput:** Sustain 10,000+ events/second without dropped events
- **Prediction Accuracy:** >75% precision on 30s-ahead context switch prediction (MVP target)
- **User Productivity:** 20%+ increase in continuous focus blocks (>25min) vs. baseline

### High-Level Architecture
```
┌─────────────────────────────────────────────────────────────┐
│                     User's Desktop                           │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐      │
│  │   Browser    │  │     IDE      │  │  Other Apps  │      │
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘      │
│         │                  │                  │               │
│         └──────────────────┴──────────────────┘               │
│                            │                                  │
│                            ▼                                  │
│         ┌─────────────────────────────────────┐              │
│         │   C++ Event Capture Engine          │              │
│         │  - Win32 hooks (keyboard/mouse)     │              │
│         │  - Lock-free ring buffer            │              │
│         │  - Real-time feature extraction     │              │
│         │  - Memory-mapped logging            │              │
│         └──────────┬──────────────────────────┘              │
│                    │ (IPC: ZeroMQ)                           │
│                    ▼                                          │
│         ┌─────────────────────────────────────┐              │
│         │   Python ML Inference Service       │              │
│         │  - Feature aggregation              │              │
│         │  - LSTM prediction (TorchScript)    │              │
│         │  - Intervention logic               │              │
│         └──────────┬──────────────────────────┘              │
│                    │ (WebSocket)                             │
└────────────────────┼─────────────────────────────────────────┘
                     ▼
          ┌──────────────────────────────────┐
          │   Spring Boot Backend            │
          │  - Session management            │
          │  - Historical analytics          │
          │  - A/B test configuration        │
          │  - PostgreSQL + TimescaleDB      │
          └──────────┬───────────────────────┘
                     │ (REST + WebSocket)
                     ▼
          ┌──────────────────────────────────┐
          │   React TypeScript Frontend      │
          │  - Real-time focus dashboard     │
          │  - Prediction visualization      │
          │  - Intervention controls         │
          │  - Weekly analytics              │
          └──────────────────────────────────┘
```

---

## 1. System Architecture

### 1.1 Architectural Principles

**1.1.1 Separation of Concerns (Layered Architecture)**

We apply a **strict 4-layer separation** to isolate performance-critical code from business logic:

```
┌─────────────────────────────────────────────────────────┐
│ Layer 4: Presentation (React Frontend)                  │
│ Concern: User interaction, visualization               │
│ Why: UI changes frequently; isolate from core logic    │
└─────────────────────────────────────────────────────────┘
                         ▲ REST/WebSocket
┌─────────────────────────────────────────────────────────┐
│ Layer 3: Application Logic (Spring Boot)                │
│ Concern: Business rules, data aggregation, API         │
│ Why: Platform-agnostic logic, testable without UI      │
└─────────────────────────────────────────────────────────┘
                         ▲ WebSocket
┌─────────────────────────────────────────────────────────┐
│ Layer 2: ML Inference (Python Service)                  │
│ Concern: Real-time prediction, feature engineering     │
│ Why: Python ecosystem for ML, isolated from system code│
└─────────────────────────────────────────────────────────┘
                         ▲ IPC (ZeroMQ)
┌─────────────────────────────────────────────────────────┐
│ Layer 1: Event Capture (C++ Engine)                     │
│ Concern: Low-latency data collection, OS integration   │
│ Why: Performance critical; C++ gives us microseconds   │
└─────────────────────────────────────────────────────────┘
```

**Why this matters:** If we need to swap React for Vue, or retrain the ML model with a different algorithm, or port from Windows to Linux, **each layer changes independently**. The C++ engine doesn't care about React state management; the frontend doesn't care about ring buffer implementation.

**Design Pattern:** **Hexagonal Architecture (Ports & Adapters)**  
Each layer exposes interfaces (ports) and communicates through adapters (IPC, REST). Example: The Python service exposes a `PredictionPort` interface. Today it's implemented via ZeroMQ; tomorrow we could swap to gRPC without changing the prediction logic.

---

**1.1.2 Event-Driven Architecture with Command Query Responsibility Segregation (CQRS)**

**State Model:**
```
System has TWO parallel state machines:

State Machine 1: Command Side (Writes)
┌─────────────┐  Event   ┌─────────────┐  Persist  ┌─────────────┐
│ User Action │ ────────▶│ C++ Engine  │ ─────────▶│  Event Log  │
│(typing, etc)│          │ (Producer)  │           │ (Immutable) │
└─────────────┘          └─────────────┘           └─────────────┘

State Machine 2: Query Side (Reads)
┌─────────────┐  Read    ┌─────────────┐  Aggregate ┌─────────────┐
│  Event Log  │ ────────▶│ ML Service  │ ──────────▶│ Predictions │
│             │          │ (Consumer)  │            │   (Views)   │
└─────────────┘          └─────────────┘            └─────────────┘
```

**Why CQRS?**  
- **Write path** (C++ engine logging events) is optimized for throughput and latency - no reads allowed
- **Read path** (ML service consuming events) can lag by 10-50ms without impacting capture performance
- If the ML service crashes, event capture continues unaffected; predictions resume when service restarts

**Pattern in Practice:**  
The C++ engine writes events to a memory-mapped file (append-only log). The Python service tails this log asynchronously. This is the **Event Sourcing** pattern - we store *events* (user typed at 14:32:01.523) rather than *state* (user is focused).

---

**1.1.3 Back-Pressure and Flow Control**

**Problem:** What if the ML service can't keep up with 10k events/second?

**Solution: Lock-Free Ring Buffer with Producer Back-Pressure**

```
Producer (C++ hooks):          Consumer (Python service):
┌──────────────────┐          ┌──────────────────┐
│ Event arrives    │          │ Read from buffer │
│ ↓                │          │ ↓                │
│ Try write to buf │──────────▶│ Process event   │
│ ↓                │  Buffer  │ ↓                │
│ Success? Log it  │  Full?   │ Advance read ptr│
│ ↓                │  Drop!   │                  │
│ Continue         │◀──────────│ (no blocking)   │
└──────────────────┘          └──────────────────┘
```

**Key Decision:** If the buffer fills (consumer too slow), **we drop events rather than block**.  

**Why?** Blocking the producer would freeze system event hooks, causing user input lag (typing feels sluggish). Better to miss some events than make the system unusable. We monitor drop rate and alert if it exceeds 0.1%.

**Pattern:** **Bounded Queue + Fail-Fast** (vs. unbounded queue that could exhaust memory)

---

### 1.2 Component Responsibilities

#### 1.2.1 C++ Event Capture Engine

**Responsibility:** Capture, classify, and log every user interaction with <1ms latency.

**State Machine:**
```
┌───────────┐  Event    ┌───────────┐  Classify  ┌───────────┐  Enqueue  ┌───────────┐
│  OS Hook  │ ────────▶ │  Filter   │ ──────────▶│  Extract  │ ─────────▶│  Buffer   │
│  Trigger  │           │  (valid?) │            │  Features │           │  (ring)   │
└───────────┘           └───────────┘            └───────────┘           └───────────┘
                              │                          │
                              │ Invalid: Drop            │
                              ▼                          ▼
                        ┌───────────┐              ┌───────────┐
                        │  Discard  │              │  Logger   │
                        └───────────┘              │  (mmap)   │
                                                   └───────────┘
```

**Key Design Decisions:**

1. **Win32 Hooks vs. Polling:**  
   - **Choice:** Event-driven hooks (`SetWindowsHookEx`)
   - **Why:** Polling at 100Hz would use 5-10% CPU; hooks are triggered only when events occur (<0.5% CPU)
   - **Trade-off:** Hooks require careful thread safety (we run them on dedicated threads with lock-free queues)

2. **Lock-Free Ring Buffer:**
   - **Pattern:** Single-Producer-Single-Consumer (SPSC) lock-free queue
   - **Implementation:** Circular buffer with atomic head/tail pointers
   - **Why:** Traditional mutexes add 10-50μs contention; atomic operations are 10-100ns
   ```cpp
   struct Event { uint64_t timestamp; EventType type; uint32_t data[8]; };
   
   alignas(64) std::atomic<uint64_t> head; // Producer writes here
   alignas(64) std::atomic<uint64_t> tail; // Consumer reads from here
   std::array<Event, 65536> buffer;        // Power of 2 for fast modulo
   ```
   - **Cache Line Alignment:** `alignas(64)` prevents false sharing (head/tail on separate cache lines)

3. **Memory-Mapped File Logging:**
   - **Pattern:** Write-Ahead Log (WAL)
   - **Why:** `fwrite()` copies to kernel buffer (2 syscalls); mmap writes directly to page cache (0 syscalls)
   - **Performance:** 5-10μs per write vs. 50-100μs with traditional I/O
   ```cpp
   void* log_base = mmap(NULL, LOG_SIZE, PROT_WRITE, MAP_SHARED, fd, 0);
   memcpy(log_base + offset, &event, sizeof(Event)); // Direct memory write
   ```

**Educational Note - States in Systems Programming:**  
The ring buffer has 3 possible states:
1. **Empty** (head == tail): Consumer waits
2. **Partially Full** (head > tail, wrapping handled): Normal operation
3. **Full** (head + 1 == tail): Back-pressure activated

We use **atomic compare-and-swap (CAS)** to transition between states without locks:
```cpp
uint64_t expected = head.load(std::memory_order_relaxed);
uint64_t desired = expected + 1;
if (head.compare_exchange_weak(expected, desired)) {
    // Successfully advanced head, write event
}
```

---

#### 1.2.2 Python ML Inference Service

**Responsibility:** Consume events, aggregate features, predict distraction risk, trigger interventions.

**State Machine:**
```
┌──────────────┐  Batch   ┌──────────────┐  Predict  ┌──────────────┐
│ Event Buffer │ ───────▶ │ Feature Eng. │ ────────▶ │ LSTM Model   │
│ (10s window) │  Ready   │ (rolling avg)│           │ (TorchScript)│
└──────────────┘          └──────────────┘           └──────┬───────┘
                                                             │
                                                             ▼
┌──────────────┐  Risk>0.7  ┌──────────────┐  Execute ┌──────────────┐
│  No Action   │ ◀───────── │  Intervention│ ────────▶│ Block Sites  │
│              │    Low     │  Decision    │   High   │ Kill Procs   │
└──────────────┘            └──────────────┘          └──────────────┘
```

**Key Design Decisions:**

1. **Batch vs. Stream Processing:**
   - **Hybrid Approach:** Batch features every 10s, but trigger immediate inference if high-risk event detected (e.g., opening browser)
   - **Why:** Constant inference wastes CPU; batching amortizes overhead
   - **Pattern:** **Micro-batching** (Storm/Spark Streaming concept)

2. **Feature Engineering Pipeline:**
   ```
   Raw Events (1000/sec) → Time Windows → Statistical Features → Model Input
   
   Example:
   Keystrokes: [t:0, t:0.05, t:0.12, ...]
   ↓ Window: Last 30s
   ↓ Extract: Mean inter-keystroke interval, variance, trend
   ↓ Output: [mean: 0.08s, var: 0.02, slope: +0.001]
   ```
   
   **Features We Extract (30s rolling windows):**
   - Typing cadence: mean/std/trend of inter-keystroke intervals (slowing = fatigue)
   - Mouse jitter: variance in movement speed (increasing = restlessness)
   - Context switches: count of app transitions (>3/min = distraction)
   - Idle periods: gaps >5s with no input (bathroom break vs. deep thought?)
   - Time-based: hour of day, minutes since last break (circadian patterns)

3. **Model Selection:**
   - **Phase 1 (MVP):** XGBoost on aggregated features (simpler, interpretable)
   - **Phase 2:** LSTM on event sequences (captures temporal dependencies)
   
   **Why LSTM for Phase 2?**
   ```
   XGBoost sees: [avg_typing_speed: 4.5 chars/s, switches: 2]
   LSTM sees:    [Event(typing, t=0), Event(typing, t=0.2), Event(switch, t=5), ...]
   ```
   LSTM can learn patterns like "user types fast, then slows, then switches app within 10s" = distraction sequence. XGBoost only sees the aggregate stats.

**Educational Note - Stateful vs. Stateless:**  
XGBoost is **stateless** - each prediction is independent. Feed it features, get a score.  
LSTM is **stateful** - it maintains hidden state across time steps. This means:
- We must persist LSTM hidden state between prediction calls
- Restarting the service loses context (user's behavioral trajectory)
- We handle this with **checkpointing**: Save LSTM state every 60s to disk

---

#### 1.2.3 Spring Boot Backend

**Responsibility:** Persist sessions, serve historical analytics, manage A/B tests, proxy WebSocket updates.

**API Design (RESTful + WebSocket):**

```
REST Endpoints (Synchronous):
POST   /api/v1/sessions              Create new focus session
GET    /api/v1/sessions/{id}         Retrieve session details
PATCH  /api/v1/sessions/{id}         Update session (e.g., user ended early)
GET    /api/v1/analytics/daily        Get daily aggregates (category breakdown)
GET    /api/v1/analytics/weekly       Get weekly trends
POST   /api/v1/interventions/feedback User accepted/rejected intervention

WebSocket (Asynchronous):
/ws/live                             Stream real-time updates
  ← { type: "ACTIVITY_UPDATE", app: "chrome", category: "Distraction" }
  ← { type: "PREDICTION", focus_score: 72, risk: 0.3 }
  ← { type: "INTERVENTION", action: "BLOCK_REDDIT", reason: "High risk" }
  → { type: "FEEDBACK", intervention_id: 123, accepted: false }
```

**Database Schema (PostgreSQL):**

```sql
-- Sessions: Each focus block (Pomodoro, Deep Work, or natural work period)
CREATE TABLE sessions (
    id BIGSERIAL PRIMARY KEY,
    user_id INT NOT NULL,
    start_time TIMESTAMPTZ NOT NULL,
    end_time TIMESTAMPTZ,
    category VARCHAR(50),           -- "Building", "Studying", etc.
    is_pseudo_productive BOOLEAN,   -- Educational YouTube = true
    focus_score_avg FLOAT,          -- Average prediction during session
    context_switches INT,
    interventions_triggered INT,
    interventions_accepted INT,
    created_at TIMESTAMPTZ DEFAULT NOW()
);

-- Time-series events (using TimescaleDB hypertable for performance)
CREATE TABLE events (
    time TIMESTAMPTZ NOT NULL,
    session_id BIGINT,
    event_type VARCHAR(20),  -- "KEY_PRESS", "MOUSE_MOVE", "WINDOW_SWITCH"
    app_name VARCHAR(255),
    features JSONB           -- Flexible schema for extracted features
);
SELECT create_hypertable('events', 'time');

-- Interventions: Log every blocking action
CREATE TABLE interventions (
    id BIGSERIAL PRIMARY KEY,
    session_id BIGINT REFERENCES sessions(id),
    triggered_at TIMESTAMPTZ NOT NULL,
    type VARCHAR(50),              -- "PREDICTIVE_BLOCK", "MANUAL_JAIL"
    action VARCHAR(100),           -- "BLOCK_REDDIT", "KILL_DISCORD"
    predicted_risk FLOAT,
    user_feedback VARCHAR(20),     -- "ACCEPTED", "REJECTED", "SNOOZED"
    effectiveness_score FLOAT      -- Post-hoc: did focus improve?
);
```

**Design Pattern: Repository + Service Layers**

```java
// Repository (Data Access)
@Repository
public interface SessionRepository extends JpaRepository<Session, Long> {
    List<Session> findByUserIdAndStartTimeBetween(int userId, Instant start, Instant end);
}

// Service (Business Logic)
@Service
public class AnalyticsService {
    public DailyStats getDailyAnalytics(int userId, LocalDate date) {
        List<Session> sessions = sessionRepo.findByUserIdAndStartTimeBetween(...);
        // Aggregate: total focus time, category breakdown, avg focus score
        return new DailyStats(...);
    }
}

// Controller (API)
@RestController
@RequestMapping("/api/v1/analytics")
public class AnalyticsController {
    @GetMapping("/daily")
    public ResponseEntity<DailyStats> getDailyStats(@RequestParam LocalDate date) {
        return ResponseEntity.ok(analyticsService.getDailyAnalytics(currentUser(), date));
    }
}
```

**Why this layering?**  
- Repository knows SQL details (JPA annotations, query optimization)
- Service knows business rules ("pseudo-productive time counts 50% toward focus score")
- Controller knows HTTP (status codes, error handling, authentication)

Each layer is testable in isolation:
- Repository: In-memory H2 database for unit tests
- Service: Mock repository, test business logic
- Controller: MockMvc for HTTP integration tests

---

#### 1.2.4 React TypeScript Frontend

**Responsibility:** Real-time visualization, user controls, intervention feedback.

**State Management Pattern: Flux with Zustand**

```typescript
// Traditional Prop Drilling (Bad):
<App>
  <Dashboard currentActivity={activity}>
    <Sidebar focusScore={score}>
      <ScoreGauge value={score} />  // Props passed through 3 levels!

// Flux Architecture (Good):
┌────────────────────────────────────────────────────────┐
│                    Global Store                        │
│  { currentActivity, focusScore, predictions, ... }     │
└────────────┬──────────────┬──────────────┬─────────────┘
             │              │              │
   ┌─────────▼────┐  ┌──────▼─────┐  ┌────▼──────┐
   │ Dashboard    │  │  Sidebar   │  │ ScoreGauge│
   │ (subscribes) │  │(subscribes)│  │(subscribes)│
   └──────────────┘  └────────────┘  └───────────┘
```

**Zustand Store Design:**

```typescript
interface FocusState {
  // Real-time data (from WebSocket)
  currentActivity: Activity | null;
  focusScore: number;          // 0-100
  distractionRisk: number;     // 0-1
  predictions: Prediction[];   // Next 5min forecast
  
  // Session state
  activeSession: Session | null;
  jailModeEnabled: boolean;
  
  // Actions (state transitions)
  updateActivity: (activity: Activity) => void;
  updatePrediction: (score: number, risk: number) => void;
  startSession: (type: "pomodoro" | "deep_work") => Promise<void>;
  endSession: () => Promise<void>;
  toggleJailMode: () => Promise<void>;
  respondToIntervention: (id: number, response: "accept" | "reject") => void;
}

const useFocusStore = create<FocusState>((set, get) => ({
  currentActivity: null,
  focusScore: 50,
  // ...
  
  updateActivity: (activity) => set({ currentActivity: activity }),
  
  startSession: async (type) => {
    const session = await api.post('/sessions', { type });
    set({ activeSession: session });
    if (type === "deep_work") {
      get().toggleJailMode();  // Auto-enable jail for deep work
    }
  },
}));
```

**Why Zustand over Redux?**
- **Simpler:** No boilerplate (actions, reducers, dispatch)
- **TypeScript-native:** Full type inference
- **Smaller:** 1KB vs. Redux 15KB + middleware
- **Hooks-based:** `const score = useFocusStore(s => s.focusScore)` (auto-subscribes to changes)

**WebSocket Integration Pattern:**

```typescript
useEffect(() => {
  const ws = new WebSocket('ws://localhost:8080/ws/live');
  
  ws.onmessage = (event) => {
    const msg = JSON.parse(event.data);
    
    switch (msg.type) {
      case 'ACTIVITY_UPDATE':
        useFocusStore.getState().updateActivity(msg.data);
        break;
      case 'PREDICTION':
        useFocusStore.getState().updatePrediction(msg.score, msg.risk);
        break;
      case 'INTERVENTION':
        // Show toast notification
        toast.warning(`${msg.action} blocked (predicted risk: ${msg.risk})`);
        break;
    }
  };
  
  return () => ws.close();  // Cleanup on unmount
}, []);
```

---

### 1.3 Inter-Component Communication

**IPC Choice Matrix:**

| Layer Boundary | Mechanism | Latency | Why |
|----------------|-----------|---------|-----|
| C++ → Python | ZeroMQ (PUB/SUB) | ~100μs | Low overhead, language-agnostic, handles back-pressure |
| Python → Spring Boot | WebSocket | ~10ms | Full-duplex, real-time updates, standard protocol |
| Spring Boot → React | WebSocket + REST | ~50ms | WebSocket for push, REST for request/response |

**ZeroMQ Pub/Sub Pattern (C++ → Python):**

```cpp
// C++ Publisher (Event Engine)
zmq::context_t ctx(1);
zmq::socket_t publisher(ctx, zmq::socket_type::pub);
publisher.bind("ipc:///tmp/neurofocus_events");

void publishEvent(const Event& event) {
    zmq::message_t msg(&event, sizeof(Event));
    publisher.send(msg, zmq::send_flags::dontwait);  // Non-blocking!
}
```

```python
# Python Subscriber (ML Service)
import zmq

ctx = zmq.Context()
subscriber = ctx.socket(zmq.SUB)
subscriber.connect("ipc:///tmp/neurofocus_events")
subscriber.subscribe(b"")  # Subscribe to all topics

while True:
    event_bytes = subscriber.recv()
    event = parse_event(event_bytes)
    process_event(event)
```

**Why ZeroMQ over gRPC?**
- **Latency:** gRPC adds ~500μs for serialization (Protobuf) + HTTP/2 framing; ZeroMQ is raw bytes
- **Simplicity:** No .proto files, no code generation
- **Pattern matching:** Pub/Sub is exactly what we need (one producer, one consumer)

---

## 2. Data Models and Schemas

### 2.1 Event Schema (C++ → Python)

**Binary Format (64 bytes, cache-line aligned):**

```cpp
struct Event {
    uint64_t timestamp_us;     // Microseconds since epoch (8 bytes)
    uint32_t event_type;       // Enum: KEY_PRESS=1, MOUSE_MOVE=2, etc. (4 bytes)
    uint32_t process_id;       // Windows PID (4 bytes)
    char app_name[32];         // Null-terminated (32 bytes)
    uint32_t window_handle;    // HWND (4 bytes)
    float data[3];             // Type-specific: [x, y, button] for mouse (12 bytes)
} __attribute__((packed));
```

**Why binary over JSON?**
- **Size:** JSON event ~200 bytes, binary 64 bytes = 3x compression
- **Parse speed:** `memcpy()` vs. JSON parser (100x faster)
- **Cache efficiency:** 64 bytes = 1 cache line (atomic read)

**Event Types:**

```cpp
enum EventType : uint32_t {
    UNKNOWN = 0,
    KEY_PRESS = 1,
    KEY_RELEASE = 2,
    MOUSE_MOVE = 3,
    MOUSE_CLICK = 4,
    WINDOW_FOCUS_CHANGE = 5,
    WINDOW_TITLE_CHANGE = 6,
    IDLE_START = 7,
    IDLE_END = 8,
};
```

---

### 2.2 Feature Vector Schema (Python ML)

**Input to LSTM Model (sequence of 30 feature vectors, 1 per second):**

```python
@dataclass
class FeatureVector:
    # Temporal features
    timestamp: datetime
    seconds_since_session_start: int
    hour_of_day: int                    # 0-23 (circadian pattern)
    minutes_since_last_break: int
    
    # Behavioral features (30s rolling windows)
    keystroke_rate: float               # Keys per second
    keystroke_variance: float           # Consistency indicator
    mouse_movement_speed: float         # Pixels per second
    mouse_jitter: float                 # Stddev of movement vector
    
    # Context features
    current_category: str               # "Building", "Studying", etc.
    context_switches_30s: int           # App changes in last 30s
    idle_time_30s: float                # Seconds of no input
    
    # Window features
    window_title_length: int            # Long titles = deep work (e.g., "file.cpp - VS Code")
    is_browser: bool
    is_ide: bool
    is_communication: bool              # Slack, Discord, Email
    
    # Derived features
    focus_momentum: float               # Exponential moving avg of past focus scores
```

**Educational Note - Feature Engineering:**  
This is **the most important part** of ML systems. The model is only as good as the features.

Example: Why `keystroke_variance`?
- **Low variance** (consistent rhythm): User is typing fluidly → focused
- **High variance** (bursts then pauses): User is distracted, typing sporadically

We discovered this by **exploratory data analysis** on our own typing patterns. You'll do the same in Phase 1.

---

### 2.3 Prediction Output Schema

```python
@dataclass
class Prediction:
    timestamp: datetime
    focus_score: float                  # 0-100 (100 = deep focus)
    distraction_risk: float             # 0-1 probability of context switch in next 30s
    risk_category: str                  # "LOW", "MEDIUM", "HIGH"
    suggested_intervention: Optional[str]  # "BLOCK_TWITTER", "SUGGEST_BREAK", None
    confidence: float                   # Model uncertainty (0-1)
    contributing_factors: List[str]     # ["Low keystroke rate", "3 switches in 30s"]
```

**Intervention Decision Logic:**

```python
def decide_intervention(prediction: Prediction, user_prefs: UserPreferences) -> Optional[Intervention]:
    if prediction.risk_category == "LOW":
        return None
    
    if prediction.risk_category == "HIGH" and user_prefs.predictive_blocking_enabled:
        # Proactive blocking
        if "browser" in prediction.contributing_factors:
            return Intervention("BLOCK_DISTRACTING_SITES", sites=DISTRACTION_LIST)
        if "communication_app" in prediction.contributing_factors:
            return Intervention("BLOCK_SLACK", reason="High distraction risk")
    
    if prediction.risk_category == "MEDIUM":
        # Gentle nudge
        return Intervention("SHOW_NOTIFICATION", message="You've been focused for 45min. Consider a break soon?")
    
    return None
```

---

## 3. Machine Learning Pipeline

### 3.1 Data Collection Phase (Weeks 1-4)

**Goal:** Collect 30+ hours of labeled data from YOUR usage.

**Labeling Strategy:**

```python
# Ground truth labels (stored in sessions table)
class FocusLabel(Enum):
    DEEP_FOCUS = 2       # Completed focus session, no context switches
    PRODUCTIVE = 1       # Working, occasional switches, stayed on task
    PSEUDO_PRODUCTIVE = 0  # Educational content but not goal-directed
    DISTRACTED = -1      # Left focus session early, high context switches
```

**How to label:**
1. **Automatic (timer-based):**
   - User completes 25min Pomodoro without disabling jail → `PRODUCTIVE`
   - User completes 90min Deep Work → `DEEP_FOCUS`
   - User disables jail <10min into session → `DISTRACTED`

2. **Manual (hotkey-based):**
   - User presses `Ctrl+Shift+F` → Label current 5min as `DEEP_FOCUS`
   - User presses `Ctrl+Shift+D` → Label current 5min as `DISTRACTED`

3. **Retrospective (end-of-session survey):**
   - Popup after session: "Rate your focus (1-5)"
   - 5 → `DEEP_FOCUS`, 4 → `PRODUCTIVE`, 3 → `PSEUDO_PRODUCTIVE`, 1-2 → `DISTRACTED`

**Educational Note - Supervised Learning:**  
We need labels (teacher's answers) to train the model. This is **supervised learning**.  
- **Input:** Feature vectors from a 5-minute window
- **Output:** Focus label for that window
- **Training:** Model learns "What feature patterns correspond to each label?"

Without labels, we'd have to use **unsupervised learning** (clustering), which is much harder to validate.

---

### 3.2 Model Training (Phase 1: XGBoost Baseline)

**Why XGBoost first?**
- **Fast training:** 100x faster than LSTM on small datasets (<10k samples)
- **Interpretable:** We can see feature importance (which behaviors matter most)
- **Strong baseline:** Often beats neural nets on tabular data

**Training Pipeline:**

```python
# ml/training_pipeline.py
import xgboost as xgb
from sklearn.model_selection import TimeSeriesSplit

def train_xgboost_baseline(features_df, labels_df):
    # Time-series split: Train on weeks 1-3, validate on week 4
    tscv = TimeSeriesSplit(n_splits=5)
    
    best_model = None
    best_score = 0
    
    for train_idx, val_idx in tscv.split(features_df):
        X_train, X_val = features_df.iloc[train_idx], features_df.iloc[val_idx]
        y_train, y_val = labels_df.iloc[train_idx], labels_df.iloc[val_idx]
        
        model = xgb.XGBClassifier(
            max_depth=6,
            learning_rate=0.1,
            n_estimators=100,
            objective='multi:softmax',
            num_class=4  # 4 focus labels
        )
        
        model.fit(X_train, y_train)
        score = model.score(X_val, y_val)
        
        if score > best_score:
            best_score = score
            best_model = model
    
    # Feature importance analysis
    plot_feature_importance(best_model)
    
    return best_model
```

**Educational Note - Time Series Split:**  
We CANNOT use regular k-fold cross-validation because that shuffles data randomly.  
**Problem:** Model could "see the future" (trained on data from after the prediction time).

**Time Series Split** respects temporal order:
```
Fold 1: Train [Week 1      ] → Test [Week 2]
Fold 2: Train [Week 1-2    ] → Test [Week 3]
Fold 3: Train [Week 1-3    ] → Test [Week 4]
```

This simulates real deployment: model only knows the past when predicting the future.

---

### 3.3 Model Training (Phase 2: LSTM for Sequences)

**Why LSTM?**  
XGBoost sees aggregated features (avg keystroke rate). LSTM sees the raw sequence:

```
XGBoost input:  [avg_keystrokes: 3.2, avg_mouse_speed: 150]
LSTM input:     [(key, t=0), (key, t=0.3), (mouse, t=1.2), (switch, t=5.0), ...]
```

LSTM can learn temporal patterns: "User types rapidly, then mouse movement slows, then switches app within 30s" = distraction signature.

**Architecture:**

```python
import torch
import torch.nn as nn

class FocusPredictor(nn.Module):
    def __init__(self, input_size=20, hidden_size=128, num_layers=2, num_classes=4):
        super().__init__()
        
        # LSTM layer (processes sequences)
        self.lstm = nn.LSTM(
            input_size=input_size,      # 20 features per timestep
            hidden_size=hidden_size,    # 128 hidden units (model capacity)
            num_layers=num_layers,      # Stack 2 LSTM layers
            batch_first=True,           # Input shape: (batch, seq_len, features)
            dropout=0.2                 # Regularization
        )
        
        # Attention layer (focus on important timesteps)
        self.attention = nn.Linear(hidden_size, 1)
        
        # Output layer (classification)
        self.fc = nn.Linear(hidden_size, num_classes)
    
    def forward(self, x):
        # x shape: (batch_size, seq_len=30, features=20)
        
        # LSTM processes sequence
        lstm_out, (hidden, cell) = self.lstm(x)  # (batch, 30, 128)
        
        # Attention: weight each timestep's importance
        attn_weights = torch.softmax(self.attention(lstm_out), dim=1)  # (batch, 30, 1)
        context = torch.sum(attn_weights * lstm_out, dim=1)  # (batch, 128)
        
        # Classification
        logits = self.fc(context)  # (batch, 4)
        return logits
```

**Educational Note - LSTM State:**  
LSTM has "memory cells" that carry information across time steps.

```
t=0: User types "def hello" → LSTM state: [0.2, 0.5, ...]
t=1: User types "()"        → LSTM updates state: [0.3, 0.6, ...] (retains context)
t=2: Mouse moves to browser → LSTM: [0.1, 0.9, ...] (detects behavior shift)
```

The hidden state is a **compressed representation** of "everything the model remembers" about the user's recent behavior.

---

### 3.4 Evaluation Metrics

**Why not just accuracy?**  
Imagine 95% of time you're focused, 5% distracted. A model that always predicts "focused" gets 95% accuracy but is useless.

**Metrics we care about:**

1. **Precision @ K (for interventions):**
   - Of the top K highest-risk predictions, how many were actually distractions?
   - **Why:** We only intervene on high-risk predictions, so we care about precision in that regime
   - **Target:** >80% precision in top 10% of predictions

2. **Recall for Distracted class:**
   - Of all actual distractions, what % did we predict?
   - **Why:** Missing distractions = user loses focus unnecessarily
   - **Target:** >70% recall (catch 7/10 distractions)

3. **Temporal Tolerance:**
   - Did we predict distraction within 30s before it happened?
   - **Why:** Predicting 5min early is too intrusive; 5s before is too late
   - **Target:** 60% of predictions in 15-45s window

4. **False Positive Cost:**
   - How often do we block when user was actually focused?
   - **Why:** False alarms erode trust, user disables system
   - **Target:** <5% false positive rate in HIGH risk category

**Evaluation Code:**

```python
def evaluate_model(model, test_data):
    predictions = model.predict(test_data.features)
    
    # Precision @ K
    top_k_indices = np.argsort(predictions[:, DISTRACTED_CLASS])[-100:]  # Top 100 riskiest
    precision_at_k = np.mean(test_data.labels[top_k_indices] == DISTRACTED)
    
    # Recall
    distracted_mask = test_data.labels == DISTRACTED
    recall = np.mean(predictions[distracted_mask, DISTRACTED_CLASS] > 0.7)
    
    # Temporal analysis
    temporal_accuracy = compute_temporal_tolerance(predictions, test_data.timestamps)
    
    print(f"Precision@100: {precision_at_k:.2%}")
    print(f"Distraction Recall: {recall:.2%}")
    print(f"Temporal Accuracy (30s window): {temporal_accuracy:.2%}")
```

---

## 4. Performance Requirements

### 4.1 Latency Budgets

**End-to-End Latency (Event → Intervention):**

```
Target: <100ms p99

Breakdown:
1. OS Hook → C++ Buffer:      0.5ms  (Win32 callback overhead)
2. C++ Feature Extraction:    0.3ms  (SIMD-optimized)
3. IPC (ZeroMQ):              0.1ms  (shared memory transport)
4. Python Feature Aggregation: 2ms   (NumPy operations)
5. LSTM Inference:            5ms    (TorchScript on CPU)
6. Intervention Decision:     1ms    (rule evaluation)
7. WebSocket Push:            3ms    (localhost connection)
-------------------------------------------
Total:                        ~12ms p50, <100ms p99
```

**Monitoring:**

```cpp
// C++ latency tracking
struct LatencyStats {
    std::array<uint64_t, 10000> samples;  // Ring buffer of last 10k latencies
    size_t index = 0;
    
    void record(uint64_t latency_us) {
        samples[index++ % 10000] = latency_us;
    }
    
    uint64_t p99() {
        auto sorted = samples;
        std::sort(sorted.begin(), sorted.end());
        return sorted[9900];  // 99th percentile
    }
};
```

---

### 4.2 Throughput Requirements

**Sustained Load:**
- 10,000 events/second (worst case: rapid typing + mouse movement)
- 0.01% drop rate (<1 event per 10k)

**Peak Load:**
- 50,000 events/second (burst during copy-paste or scrolling)
- Handled via ring buffer (65,536 capacity = 1.3s buffer at 50k/s)

**Scalability Test:**

```python
# Generate synthetic load
def stress_test():
    for rate in [1000, 5000, 10000, 50000]:
        print(f"Testing {rate} events/sec...")
        
        start = time.time()
        for _ in range(rate * 10):  # 10 seconds of events
            inject_event(random_event())
        
        elapsed = time.time() - start
        drops = get_dropped_event_count()
        
        print(f"  Elapsed: {elapsed:.2f}s (expected 10s)")
        print(f"  Dropped: {drops} ({drops/(rate*10)*100:.3f}%)")
```

---

## 5. Security and Privacy

### 5.1 Data Minimization

**What we capture:**
- ✅ Window titles (e.g., "main.py - VS Code")
- ✅ Process names (e.g., "chrome.exe")
- ✅ Timing metadata (keystroke intervals, idle periods)
- ✅ Aggregated statistics (avg typing speed)

**What we DON'T capture:**
- ❌ Keystroke content (which keys were pressed)
- ❌ Mouse coordinates (only speed/jitter)
- ❌ Screenshots
- ❌ Network traffic

**Privacy Pattern: Metadata-Only Logging**

```cpp
// BAD: Captures content
struct KeyEvent {
    char key;  // 'a', 'b', 'p', 'a', 's', 's', 'w', 'o', 'r', 'd' → password leaked!
};

// GOOD: Captures only timing
struct KeyEvent {
    uint64_t timestamp_us;
    // No key identity stored
};
```

---

### 5.2 Local-First Architecture

**All data stays on user's machine:**
- C++ logs to local disk (`C:\Users\<user>\AppData\Local\NeuralFocus\events.log`)
- Python models trained locally
- PostgreSQL database is local (`localhost:5432`)

**No cloud transmission** (Phase 1-3). Federated learning (Phase 4) will use **differential privacy** to share only model gradients, not raw data.

---

## 6. Development Roadmap

### Month 1: MVP Data Collection

**Week 1: C++ Event Engine Skeleton**
- [ ] Win32 hook setup (keyboard, mouse, window focus)
- [ ] Lock-free ring buffer implementation
- [ ] Memory-mapped file logger
- [ ] Latency profiling (target: <1ms p99)

**Week 2: Python Data Processing**
- [ ] ZeroMQ subscriber
- [ ] Feature extraction pipeline (rolling windows)
- [ ] CSV export for training data
- [ ] Ground truth labeling UI (hotkey + retrospective)

**Week 3: Manual Data Collection**
- [ ] Run system for 1 week, collect YOUR data
- [ ] Analyze patterns (EDA: typing speed vs. focus)
- [ ] Refine feature engineering based on insights

**Week 4: XGBoost Baseline**
- [ ] Train model on collected data
- [ ] Evaluate precision/recall
- [ ] If >70% accuracy → proceed to Phase 2; else iterate on features

---

### Month 2: Real-Time Prediction

**Week 5-6: LSTM Training**
- [ ] Implement sequence model (PyTorch)
- [ ] TorchScript compilation for faster inference
- [ ] Integrate with C++ event stream (online inference)

**Week 7: Intervention Logic**
- [ ] Implement blocking system (hosts file, process killer)
- [ ] Predictive intervention (trigger on risk >0.7)
- [ ] Feedback loop (did intervention help?)

**Week 8: Performance Optimization**
- [ ] Profile end-to-end latency
- [ ] Optimize bottlenecks (SIMD, batch inference)
- [ ] Load testing (10k events/sec)

---

### Month 3: Full-Stack UI

**Week 9-10: Spring Boot Backend**
- [ ] PostgreSQL schema setup
- [ ] REST API implementation
- [ ] WebSocket server for real-time updates
- [ ] A/B test framework (compare intervention strategies)

**Week 11-12: React Frontend**
- [ ] Dashboard layout (focus score, timer, activity log)
- [ ] Real-time charts (Chart.js or Recharts)
- [ ] Intervention notifications (toast alerts)
- [ ] Analytics views (weekly trends)

---

### Month 4 (Optional): Federated Learning

**Week 13-14: Multi-User Architecture**
- [ ] User registration and authentication
- [ ] Gradient aggregation server (PySyft/Flower)
- [ ] Differential privacy implementation

**Week 15-16: Publication**
- [ ] Write technical blog post
- [ ] Prepare demo video
- [ ] Submit to alignment forum / ML conferences

---

## 7. Open Questions and Risks

### 7.1 Technical Risks

**Risk 1: Insufficient Training Data**
- **Problem:** Need 30+ hours of labeled data; collecting from one user is slow
- **Mitigation:** Start with rule-based system (block sites after 3 switches), layer in ML after 2 weeks

**Risk 2: Model Doesn't Generalize**
- **Problem:** Trained on YOUR patterns, may not work for other users
- **Mitigation:** Phase 4 federated learning; or accept this as single-user tool for MVP

**Risk 3: C++ Performance Doesn't Matter**
- **Problem:** Python might be fast enough; C++ adds complexity
- **Mitigation:** Prototype in Python first (Week 1-2), measure latency, only port to C++ if >10ms p99

---

### 7.2 Design Decisions Needed

**Decision 1: IPC Mechanism**
- Option A: ZeroMQ (recommended, battle-tested)
- Option B: Shared memory (faster, complex to debug)
- Option C: gRPC (slower, standard tooling)

**Decision 2: Frontend Framework**
- Option A: React + Zustand (recommended, modern)
- Option B: Vue + Pinia (smaller, less ecosystem)
- Option C: Electron desktop app vs. local web server

**Decision 3: Model Complexity**
- Option A: XGBoost only (simpler, interpretable)
- Option B: LSTM (better accuracy, harder to debug)
- Option C: Hybrid (XGBoost for fast path, LSTM for hard cases)

---

## 8. Success Criteria

**Phase 1 (Month 1): Data Collection**
- ✅ Capture 30+ hours of labeled data
- ✅ Achieve <1ms p99 latency for event capture
- ✅ XGBoost baseline >70% accuracy on 30s-ahead prediction

**Phase 2 (Month 2): Real-Time Prediction**
- ✅ LSTM model >75% precision @ top 10% predictions
- ✅ End-to-end latency <100ms p99
- ✅ Sustain 10,000 events/sec without drops

**Phase 3 (Month 3): Full-Stack Product**
- ✅ Working web UI with real-time updates
- ✅ Predictive interventions reduce distractions by 20%
- ✅ System runs 8 hours/day without crashes

**Phase 4 (Month 4, Optional): Federated Learning**
- ✅ 3+ beta users contributing data
- ✅ Aggregate model outperforms single-user model by 10%
- ✅ Differential privacy audit (ε < 1.0)

---

## 9. Appendix: Key Algorithms

### 9.1 Lock-Free Ring Buffer (C++)

```cpp
template<typename T, size_t Size>
class LockFreeRingBuffer {
    static_assert((Size & (Size - 1)) == 0, "Size must be power of 2");
    
    alignas(64) std::atomic<uint64_t> head_{0};  // Producer writes here
    alignas(64) std::atomic<uint64_t> tail_{0};  // Consumer reads from here
    std::array<T, Size> buffer_;
    
public:
    bool try_push(const T& item) {
        uint64_t head = head_.load(std::memory_order_relaxed);
        uint64_t next_head = head + 1;
        
        if (next_head - tail_.load(std::memory_order_acquire) >= Size) {
            return false;  // Buffer full
        }
        
        buffer_[head & (Size - 1)] = item;
        head_.store(next_head, std::memory_order_release);
        return true;
    }
    
    bool try_pop(T& item) {
        uint64_t tail = tail_.load(std::memory_order_relaxed);
        
        if (tail == head_.load(std::memory_order_acquire)) {
            return false;  // Buffer empty
        }
        
        item = buffer_[tail & (Size - 1)];
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }
};
```

**Educational Note - Memory Ordering:**
- `relaxed`: No synchronization (fastest)
- `acquire/release`: Synchronizes memory (prevents reordering)

**Why `acquire` on `tail_.load()`?**  
Ensures we see the updated buffer contents before reading. Without it, we might read stale data due to CPU cache incoherence.

---

### 9.2 Exponential Moving Average (Feature Smoothing)

```python
def update_ema(current_value, previous_ema, alpha=0.3):
    """
    Smooth features to reduce noise.
    
    alpha = 0.1: Heavy smoothing (slow to react)
    alpha = 0.5: Balanced
    alpha = 0.9: Light smoothing (fast to react)
    """
    return alpha * current_value + (1 - alpha) * previous_ema

# Usage:
focus_score_ema = 50  # Initial value
for new_score in [45, 40, 38, 35]:  # Gradual decline
    focus_score_ema = update_ema(new_score, focus_score_ema, alpha=0.3)
    print(f"Raw: {new_score}, Smoothed: {focus_score_ema:.1f}")

# Output:
# Raw: 45, Smoothed: 48.5  (50 * 0.7 + 45 * 0.3)
# Raw: 40, Smoothed: 45.9
# Raw: 38, Smoothed: 43.7
# Raw: 35, Smoothed: 41.1
```

**Why smooth?** Single noisy event (accidental keystroke) shouldn't trigger intervention. EMA filters out spikes.

---

### 9.3 Differential Privacy (Phase 4)

```python
def add_dp_noise(gradient, epsilon=1.0, sensitivity=1.0):
    """
    Add Laplace noise to gradients before sharing.
    
    epsilon: Privacy budget (lower = more private, less accurate)
    sensitivity: Max change one user can cause
    """
    noise_scale = sensitivity / epsilon
    noise = np.random.laplace(0, noise_scale, size=gradient.shape)
    return gradient + noise

# Federated averaging
def aggregate_gradients(local_gradients, epsilon=1.0):
    noisy_gradients = [add_dp_noise(g, epsilon) for g in local_gradients]
    return np.mean(noisy_gradients, axis=0)
```

**Privacy Guarantee:**  
An adversary observing the shared gradient cannot determine if any specific user's data was included (up to probability `e^epsilon`).

---

## 10. Conclusion

This document specifies a production-grade ML system with:
- **Sub-millisecond event capture** (C++ systems programming)
- **Real-time prediction** (Python ML pipeline)
- **Full-stack UI** (React + Spring Boot)
- **Privacy-preserving design** (local-first, metadata-only)

**Next Steps:**
1. Review and approve this design
2. Set up development environment (C++ compiler, Python 3.10+, Node.js, PostgreSQL)
3. Begin Week 1 implementation (C++ event engine skeleton)

**Questions for Reviewers:**
- Is <100ms end-to-end latency sufficient, or do we need stricter targets?
- Should we prototype in Python first before committing to C++ rewrite?
- What's the minimum viable prediction accuracy to make interventions useful?

---

**Document Revision History:**
- v1.0 (2025-12-29): Initial draft

**Approval Signatures:**
- [ ] Technical Lead
- [ ] ML Engineer
- [ ] Product Manager
