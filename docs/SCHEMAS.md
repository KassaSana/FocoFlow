# Neural Focus: Data Schemas and Models

This document defines all data structures used across the system, from low-level C++ structs to high-level ML feature vectors.

## Table of Contents
1. [Event Schema (C++ ↔ Python)](#event-schema-c-python)
2. [Feature Vector Schema (ML Input)](#feature-vector-schema-ml-input)
3. [Prediction Schema (ML Output)](#prediction-schema-ml-output)
4. [Database Schema (PostgreSQL)](#database-schema-postgresql)
5. [API Schemas (REST/WebSocket)](#api-schemas-restwebsocket)

---

## Event Schema (C++ ↔ Python)

### Design Principles

**Why Binary Format?**
- **Performance:** JSON parsing takes 10-50μs per event; `memcpy()` takes 10-50ns (1000x faster)
- **Size:** JSON ~200 bytes, binary 64 bytes (3x compression → better cache utilization)
- **Deterministic:** Fixed size = predictable memory layout = CPU prefetcher works better

**Why 64 Bytes?**
- Exactly **1 cache line** on x86-64 CPUs (cache lines are 64 bytes)
- CPU can read entire event in one memory access (atomic read)
- Aligning to cache lines prevents "false sharing" (two threads modifying different parts of same cache line)

### Binary Layout

```cpp
// core/event.h
#pragma pack(push, 1)  // No padding between fields

struct Event {
    // ===== Temporal Data (8 bytes) =====
    uint64_t timestamp_us;     // Microseconds since epoch
                               // Why microseconds? Milliseconds too coarse
                               // for <1ms latency requirements
    
    // ===== Event Classification (8 bytes) =====
    uint32_t event_type;       // Enum: see EventType below
    uint32_t process_id;       // Windows PID (for process lookup)
    
    // ===== Application Context (32 bytes) =====
    char app_name[32];         // Null-terminated UTF-8
                               // Example: "chrome.exe\0\0\0..."
                               // 32 chars = enough for most app names
    
    // ===== Window Context (8 bytes) =====
    uint32_t window_handle;    // HWND (Windows window handle)
    uint32_t padding;          // Reserved for future use
    
    // ===== Type-Specific Data (12 bytes) =====
    union {
        // For KEY_PRESS/KEY_RELEASE
        struct {
            uint32_t virtual_key_code;  // VK_A, VK_RETURN, etc.
            uint32_t scan_code;         // Hardware scan code
            uint32_t flags;             // Alt pressed? Ctrl? Shift?
        } key;
        
        // For MOUSE_MOVE
        struct {
            int32_t x;                  // Screen coordinates
            int32_t y;
            uint32_t speed_pps;         // Pixels per second (derived)
        } mouse_move;
        
        // For MOUSE_CLICK
        struct {
            int32_t x;
            int32_t y;
            uint32_t button;            // 1=left, 2=right, 3=middle
        } mouse_click;
        
        // For WINDOW_FOCUS_CHANGE
        struct {
            uint32_t old_window;        // Previous HWND
            uint32_t new_window;        // Current HWND
            uint32_t category_hint;     // Pre-classified category
        } window_switch;
        
        // For IDLE_START/IDLE_END
        struct {
            uint32_t idle_duration_ms;  // How long was idle
            uint32_t reserved[2];
        } idle;
        
        // Raw access
        uint8_t raw_data[12];
    } data;
    
    // ===== Padding to 64 bytes =====
    uint8_t reserved[4];       // Future expansion
    
} __attribute__((packed, aligned(64)));
// packed: No padding (exactly 64 bytes)
// aligned(64): Start on cache line boundary

#pragma pack(pop)

static_assert(sizeof(Event) == 64, "Event must be exactly 64 bytes");
```

### EventType Enum

```cpp
// core/event_types.h
enum EventType : uint32_t {
    UNKNOWN = 0,              // Should never occur (error state)
    
    // Keyboard events
    KEY_PRESS = 1,
    KEY_RELEASE = 2,
    
    // Mouse events
    MOUSE_MOVE = 3,
    MOUSE_CLICK = 4,
    MOUSE_WHEEL = 5,
    
    // Window events
    WINDOW_FOCUS_CHANGE = 6,  // User switched apps
    WINDOW_TITLE_CHANGE = 7,  // Same app, different document
    WINDOW_MINIMIZE = 8,
    WINDOW_MAXIMIZE = 9,
    
    // Idle detection
    IDLE_START = 10,          // No input for 5+ seconds
    IDLE_END = 11,
    
    // System events
    SCREEN_LOCK = 12,         // User locked screen
    SCREEN_UNLOCK = 13,
    
    // Future expansion
    RESERVED_14 = 14,
    RESERVED_15 = 15,
};
```

### Python Deserialization

```python
# ml/event_parser.py
import struct
from dataclasses import dataclass
from enum import IntEnum
from datetime import datetime

class EventType(IntEnum):
    KEY_PRESS = 1
    KEY_RELEASE = 2
    MOUSE_MOVE = 3
    MOUSE_CLICK = 4
    WINDOW_FOCUS_CHANGE = 6
    IDLE_START = 10
    IDLE_END = 11

@dataclass
class Event:
    timestamp: datetime
    event_type: EventType
    process_id: int
    app_name: str
    window_handle: int
    data: dict  # Type-specific data

def parse_event(raw_bytes: bytes) -> Event:
    """
    Parse 64-byte binary event into Python object.
    
    Educational Note: struct.unpack format string
    - Q: unsigned long long (8 bytes) → timestamp_us
    - I: unsigned int (4 bytes) → event_type, process_id
    - 32s: 32-byte string → app_name
    - I: unsigned int → window_handle
    - I: padding
    - 12s: 12-byte data (parse separately based on event_type)
    - 4s: reserved
    """
    parts = struct.unpack('<Q I I 32s I I 12s 4s', raw_bytes)
    
    timestamp_us = parts[0]
    event_type = EventType(parts[1])
    process_id = parts[2]
    app_name = parts[3].decode('utf-8').rstrip('\x00')  # Remove null padding
    window_handle = parts[4]
    data_bytes = parts[6]
    
    # Parse type-specific data
    if event_type == EventType.KEY_PRESS:
        data = {
            'virtual_key': struct.unpack('<I', data_bytes[0:4])[0],
            'scan_code': struct.unpack('<I', data_bytes[4:8])[0],
            'flags': struct.unpack('<I', data_bytes[8:12])[0],
        }
    elif event_type == EventType.MOUSE_MOVE:
        data = {
            'x': struct.unpack('<i', data_bytes[0:4])[0],  # signed int
            'y': struct.unpack('<i', data_bytes[4:8])[0],
            'speed_pps': struct.unpack('<I', data_bytes[8:12])[0],
        }
    elif event_type == EventType.WINDOW_FOCUS_CHANGE:
        data = {
            'old_window': struct.unpack('<I', data_bytes[0:4])[0],
            'new_window': struct.unpack('<I', data_bytes[4:8])[0],
            'category_hint': struct.unpack('<I', data_bytes[8:12])[0],
        }
    else:
        data = {}
    
    return Event(
        timestamp=datetime.fromtimestamp(timestamp_us / 1_000_000),
        event_type=event_type,
        process_id=process_id,
        app_name=app_name,
        window_handle=window_handle,
        data=data
    )
```

**Educational Note: Endianness (`<` prefix)**

```python
struct.unpack('<Q', ...)  # Little-endian
# vs
struct.unpack('>Q', ...)  # Big-endian
```

**What's endianness?**  
The byte order for multi-byte values:

```
Number: 0x12345678 (4 bytes)

Little-endian (x86/x64): [78 56 34 12]  ← LSB first
Big-endian (network):    [12 34 56 78]  ← MSB first
```

x86/x64 CPUs use little-endian, so we specify `<` to match C++ memory layout.

---

## Feature Vector Schema (ML Input)

### Design Philosophy

**What Makes a Good Feature?**

1. **Discriminative:** Separates focused from distracted states
2. **Stable:** Doesn't fluctuate wildly due to noise
3. **Causal:** Can't use "future information" (would leak ground truth)
4. **Interpretable:** We can explain why it matters

**Bad Feature Example:**
```python
total_keystrokes_today = 12450  # Doesn't tell us current focus state
```

**Good Feature Example:**
```python
keystroke_rate_last_30s = 4.2   # Recent activity level
keystroke_trend_last_5min = -0.3  # Slowing down? (fatigue signal)
```

### Feature Vector Structure

```python
# ml/features.py
from dataclasses import dataclass
from typing import Optional

@dataclass
class FeatureVector:
    """
    All features are computed over rolling time windows.
    Default window: 30 seconds (unless specified).
    """
    
    # ========== Temporal Features ==========
    timestamp: float                      # Unix timestamp
    seconds_since_session_start: int      # 0, 1, 2, ... up to 5400 (90min)
    hour_of_day: int                      # 0-23 (circadian rhythm)
    day_of_week: int                      # 0=Monday, 6=Sunday
    minutes_since_last_break: int         # Fatigue accumulation
    
    # ========== Keystroke Features (30s window) ==========
    keystroke_count: int                  # Raw count
    keystroke_rate: float                 # Events per second
    keystroke_interval_mean: float        # Avg time between keys (seconds)
    keystroke_interval_std: float         # Consistency (low = rhythmic)
    keystroke_interval_trend: float       # Linear regression slope
                                          # Negative = slowing down (fatigue)
    
    # ========== Mouse Features (30s window) ==========
    mouse_move_count: int                 # Raw count
    mouse_distance_pixels: float          # Total pixels traveled
    mouse_speed_mean: float               # Avg pixels/second
    mouse_speed_std: float                # "Jitter" - high = restless
    mouse_acceleration_mean: float        # Change in speed (erratic = distracted)
    mouse_click_count: int                # Interaction frequency
    
    # ========== Context Switch Features ==========
    context_switches_30s: int             # App changes in last 30s
    context_switches_5min: int            # Broader pattern
    time_in_current_app: int              # Seconds in current app (stability)
    unique_apps_5min: int                 # Breadth of attention
    
    # ========== Idle Features ==========
    idle_time_30s: float                  # Seconds of no input
    idle_event_count_5min: int            # Number of idle periods
    longest_active_stretch_5min: int      # Max seconds without idle
    
    # ========== Window Features ==========
    window_title_length: int              # Long titles = specific work
                                          # "main.py - VS Code" (18 chars)
                                          # vs "New Tab" (7 chars)
    window_title_changed_30s: bool        # Document switch in same app
    
    # ========== Application Category Features ==========
    is_browser: bool                      # Chrome, Firefox, Edge
    is_ide: bool                          # VS Code, IntelliJ, PyCharm
    is_communication: bool                # Slack, Discord, Email
    is_entertainment: bool                # YouTube, Spotify, Games
    is_productivity: bool                 # Word, Excel, Notion
    
    # ========== Derived Features ==========
    focus_momentum: float                 # EMA of past focus scores
                                          # 0-100, updated after each prediction
    productivity_category: str            # "Building", "Studying", "Knowledge"
    is_pseudo_productive: bool            # Educational YouTube, LinkedIn, etc.
    
    # ========== Sequence Features (for LSTM) ==========
    recent_event_sequence: Optional[list] # Last 10 events (types only)
                                          # [KEY, KEY, MOUSE, SWITCH, ...]
    
    def to_numpy(self):
        """Convert to numpy array for model input."""
        import numpy as np
        
        return np.array([
            self.seconds_since_session_start,
            self.hour_of_day,
            self.day_of_week,
            self.minutes_since_last_break,
            self.keystroke_rate,
            self.keystroke_interval_mean,
            self.keystroke_interval_std,
            self.keystroke_interval_trend,
            self.mouse_speed_mean,
            self.mouse_speed_std,
            self.mouse_acceleration_mean,
            self.mouse_click_count,
            self.context_switches_30s,
            self.context_switches_5min,
            self.time_in_current_app,
            self.unique_apps_5min,
            self.idle_time_30s,
            self.idle_event_count_5min,
            self.longest_active_stretch_5min,
            self.window_title_length,
            float(self.is_browser),
            float(self.is_ide),
            float(self.is_communication),
            float(self.is_entertainment),
            float(self.is_productivity),
            self.focus_momentum,
            float(self.is_pseudo_productive),
        ], dtype=np.float32)
```

### Feature Engineering Pipeline

```python
# ml/feature_engineering.py
from collections import deque
from typing import List
import numpy as np

class FeatureExtractor:
    """
    Maintains rolling windows and computes features in real-time.
    
    Design Pattern: Online Algorithm (streaming data)
    - We can't "recompute from scratch" every time (too slow)
    - Instead, maintain state and update incrementally
    """
    
    def __init__(self, window_size_seconds=30):
        self.window_size = window_size_seconds
        
        # Rolling windows (deque = efficient for append/pop)
        self.events_30s = deque(maxlen=1000)  # Last 30s of events
        self.events_5min = deque(maxlen=10000)  # Last 5min
        
        # Cached statistics (update incrementally)
        self.keystroke_intervals = []
        self.mouse_speeds = []
        self.app_switches = []
        
    def add_event(self, event: Event):
        """
        Add event to rolling windows and evict old events.
        
        Time Complexity: O(1) amortized
        - deque append: O(1)
        - Eviction: O(k) where k = events older than window
        """
        current_time = event.timestamp.timestamp()
        
        # Add to windows
        self.events_30s.append(event)
        self.events_5min.append(event)
        
        # Evict old events (outside window)
        cutoff_30s = current_time - 30
        cutoff_5min = current_time - 300
        
        while self.events_30s and self.events_30s[0].timestamp.timestamp() < cutoff_30s:
            self.events_30s.popleft()
        
        while self.events_5min and self.events_5min[0].timestamp.timestamp() < cutoff_5min:
            self.events_5min.popleft()
    
    def extract_features(self) -> FeatureVector:
        """
        Compute feature vector from current window state.
        
        Educational Note: Statistical Aggregations
        """
        if not self.events_30s:
            return FeatureVector(...)  # Default/zero features
        
        # Filter events by type
        keystrokes = [e for e in self.events_30s if e.event_type in [EventType.KEY_PRESS]]
        mouse_moves = [e for e in self.events_30s if e.event_type == EventType.MOUSE_MOVE]
        app_switches = [e for e in self.events_5min if e.event_type == EventType.WINDOW_FOCUS_CHANGE]
        
        # Keystroke features
        if len(keystrokes) >= 2:
            intervals = [
                (keystrokes[i].timestamp - keystrokes[i-1].timestamp).total_seconds()
                for i in range(1, len(keystrokes))
            ]
            keystroke_interval_mean = np.mean(intervals)
            keystroke_interval_std = np.std(intervals)
            
            # Trend: fit linear regression y = mx + b, return slope m
            # Positive slope = speeding up, Negative = slowing down
            x = np.arange(len(intervals))
            keystroke_interval_trend = np.polyfit(x, intervals, deg=1)[0]
        else:
            keystroke_interval_mean = 0
            keystroke_interval_std = 0
            keystroke_interval_trend = 0
        
        # Mouse features
        if mouse_moves:
            speeds = [e.data['speed_pps'] for e in mouse_moves]
            mouse_speed_mean = np.mean(speeds)
            mouse_speed_std = np.std(speeds)
            
            # Acceleration = change in speed
            accelerations = [speeds[i] - speeds[i-1] for i in range(1, len(speeds))]
            mouse_acceleration_mean = np.mean(accelerations) if accelerations else 0
        else:
            mouse_speed_mean = 0
            mouse_speed_std = 0
            mouse_acceleration_mean = 0
        
        # Context switch features
        context_switches_30s = len([e for e in self.events_30s if e.event_type == EventType.WINDOW_FOCUS_CHANGE])
        context_switches_5min = len(app_switches)
        unique_apps_5min = len(set(e.app_name for e in self.events_5min))
        
        # Current app stability
        current_app = self.events_30s[-1].app_name if self.events_30s else ""
        time_in_current_app = sum(
            1 for e in self.events_30s if e.app_name == current_app
        ) * 30 / len(self.events_30s) if self.events_30s else 0
        
        # Window title
        window_title_length = len(current_app)  # Simplified (would extract real title in prod)
        
        # Category classification (simplified)
        is_browser = current_app.lower() in ["chrome.exe", "firefox.exe", "msedge.exe"]
        is_ide = current_app.lower() in ["code.exe", "devenv.exe", "pycharm.exe"]
        
        return FeatureVector(
            timestamp=time.time(),
            seconds_since_session_start=0,  # Set by session manager
            hour_of_day=datetime.now().hour,
            day_of_week=datetime.now().weekday(),
            minutes_since_last_break=0,  # Set by session manager
            keystroke_count=len(keystrokes),
            keystroke_rate=len(keystrokes) / 30,
            keystroke_interval_mean=keystroke_interval_mean,
            keystroke_interval_std=keystroke_interval_std,
            keystroke_interval_trend=keystroke_interval_trend,
            mouse_move_count=len(mouse_moves),
            mouse_distance_pixels=0,  # TODO: compute from coordinates
            mouse_speed_mean=mouse_speed_mean,
            mouse_speed_std=mouse_speed_std,
            mouse_acceleration_mean=mouse_acceleration_mean,
            mouse_click_count=0,  # TODO
            context_switches_30s=context_switches_30s,
            context_switches_5min=context_switches_5min,
            time_in_current_app=time_in_current_app,
            unique_apps_5min=unique_apps_5min,
            idle_time_30s=0,  # TODO
            idle_event_count_5min=0,  # TODO
            longest_active_stretch_5min=0,  # TODO
            window_title_length=window_title_length,
            window_title_changed_30s=False,  # TODO
            is_browser=is_browser,
            is_ide=is_ide,
            is_communication=False,  # TODO
            is_entertainment=False,  # TODO
            is_productivity=True,  # TODO
            focus_momentum=50.0,  # TODO: update from predictions
            productivity_category="Building",  # TODO
            is_pseudo_productive=False,
            recent_event_sequence=None
        )
```

**Educational Note: Why Rolling Windows?**

Imagine we compute features over "all time":
```python
# BAD: Lifetime statistics
total_keystrokes = 125000  # Since app started
avg_typing_speed = 3.2 chars/sec  # Over 8 hours
```

**Problem:** These features are **non-stationary** - they change slowly and don't reflect current state.

Rolling windows make features **localized**:
```python
# GOOD: Recent behavior
keystrokes_last_30s = 120  # Right now
typing_speed_last_30s = 4.0 chars/sec  # Current pace
```

Now the model can detect **changes**: "User was typing at 5 chars/sec, dropped to 2 chars/sec → fatigue signal."

---

## Prediction Schema (ML Output)

```python
# ml/prediction.py
from dataclasses import dataclass
from typing import List, Optional
from enum import Enum

class RiskCategory(Enum):
    LOW = "LOW"           # <30% risk
    MEDIUM = "MEDIUM"     # 30-70% risk
    HIGH = "HIGH"         # >70% risk

@dataclass
class Prediction:
    """
    Model output: Focus state and recommended intervention.
    """
    
    # Core predictions
    timestamp: float
    focus_score: float            # 0-100 (100 = deep focus)
    distraction_risk: float       # 0-1 probability of context switch in 30s
    risk_category: RiskCategory
    
    # Model confidence
    confidence: float             # 0-1 (model uncertainty)
    prediction_horizon_seconds: int  # How far ahead (usually 30s)
    
    # Interpretability
    contributing_factors: List[str]  # Human-readable reasons
    # Example: ["Low typing speed (2.1 keys/s)", "3 app switches in 30s"]
    
    # Suggested intervention
    intervention_type: Optional[str]  # "BLOCK_SITES", "SUGGEST_BREAK", None
    intervention_target: Optional[str]  # "reddit.com, twitter.com" or None
    intervention_reason: str          # "High distraction risk detected"
    
    # For A/B testing
    model_version: str            # "xgboost_v1" or "lstm_v2"
    ab_test_group: Optional[str]  # "control", "treatment_A", etc.

def categorize_risk(risk: float) -> RiskCategory:
    """
    Convert continuous risk [0-1] to discrete category.
    
    Thresholds chosen based on empirical analysis:
    - <0.3: False alarm rate too high if we intervene
    - >0.7: Miss rate too high if we don't intervene
    """
    if risk < 0.3:
        return RiskCategory.LOW
    elif risk < 0.7:
        return RiskCategory.MEDIUM
    else:
        return RiskCategory.HIGH

def explain_prediction(features: FeatureVector, prediction: Prediction) -> List[str]:
    """
    Generate human-readable explanations for prediction.
    
    Uses heuristics + SHAP values (Phase 2).
    """
    explanations = []
    
    if features.keystroke_rate < 2.0:
        explanations.append(f"Low typing activity ({features.keystroke_rate:.1f} keys/s)")
    
    if features.context_switches_30s >= 3:
        explanations.append(f"{features.context_switches_30s} app switches in 30s")
    
    if features.mouse_speed_std > 500:
        explanations.append(f"Erratic mouse movement (jitter: {features.mouse_speed_std:.0f})")
    
    if features.is_browser and not features.is_ide:
        explanations.append("Currently in browser (high distraction risk)")
    
    if features.minutes_since_last_break > 90:
        explanations.append(f"No break in {features.minutes_since_last_break} minutes (fatigue)")
    
    return explanations or ["Pattern analysis"]
```

---

## Database Schema (PostgreSQL)

```sql
-- backend/schema.sql

-- Enable TimescaleDB extension for time-series data
CREATE EXTENSION IF NOT EXISTS timescaledb;

-- ========== User Management ==========
CREATE TABLE users (
    id SERIAL PRIMARY KEY,
    username VARCHAR(50) UNIQUE NOT NULL,
    email VARCHAR(255) UNIQUE NOT NULL,
    created_at TIMESTAMPTZ DEFAULT NOW(),
    settings JSONB DEFAULT '{}'::jsonb
);

-- ========== Focus Sessions ==========
CREATE TABLE sessions (
    id BIGSERIAL PRIMARY KEY,
    user_id INT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    
    -- Timing
    start_time TIMESTAMPTZ NOT NULL,
    end_time TIMESTAMPTZ,
    duration_seconds INT GENERATED ALWAYS AS (
        EXTRACT(EPOCH FROM (end_time - start_time))::INT
    ) STORED,
    
    -- Session type
    session_type VARCHAR(20) NOT NULL,  -- "pomodoro", "deep_work", "natural"
    jail_enabled BOOLEAN DEFAULT FALSE,
    
    -- Productivity metrics
    category VARCHAR(50),              -- "Building", "Studying", etc.
    is_pseudo_productive BOOLEAN DEFAULT FALSE,
    focus_score_avg FLOAT,             -- Average prediction during session
    focus_score_min FLOAT,
    focus_score_max FLOAT,
    
    -- Activity metrics
    total_keystrokes INT DEFAULT 0,
    total_mouse_moves INT DEFAULT 0,
    context_switches INT DEFAULT 0,
    idle_time_seconds INT DEFAULT 0,
    
    -- Intervention metrics
    interventions_triggered INT DEFAULT 0,
    interventions_accepted INT DEFAULT 0,
    interventions_rejected INT DEFAULT 0,
    
    -- Outcome
    completed BOOLEAN DEFAULT FALSE,   -- Did user complete timer?
    end_reason VARCHAR(50),            -- "timer_expired", "user_ended", "system_crash"
    
    created_at TIMESTAMPTZ DEFAULT NOW(),
    INDEX idx_sessions_user_start (user_id, start_time DESC),
    INDEX idx_sessions_category (category)
);

-- ========== Time-Series Events (Hypertable) ==========
CREATE TABLE events (
    time TIMESTAMPTZ NOT NULL,
    session_id BIGINT REFERENCES sessions(id) ON DELETE CASCADE,
    user_id INT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    
    -- Event data
    event_type VARCHAR(30) NOT NULL,   -- "KEY_PRESS", "WINDOW_SWITCH", etc.
    app_name VARCHAR(255),
    window_title VARCHAR(512),
    
    -- Features (flexible schema)
    features JSONB,
    
    -- Prediction (if available)
    focus_score FLOAT,
    distraction_risk FLOAT
);

-- Convert to TimescaleDB hypertable (partitioned by time)
SELECT create_hypertable('events', 'time', if_not_exists => TRUE);

-- Retention policy: Keep events for 90 days, then aggregate
SELECT add_retention_policy('events', INTERVAL '90 days', if_not_exists => TRUE);

-- ========== Interventions ==========
CREATE TABLE interventions (
    id BIGSERIAL PRIMARY KEY,
    session_id BIGINT REFERENCES sessions(id) ON DELETE CASCADE,
    user_id INT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    
    -- Timing
    triggered_at TIMESTAMPTZ NOT NULL,
    responded_at TIMESTAMPTZ,
    
    -- Intervention details
    type VARCHAR(50) NOT NULL,         -- "PREDICTIVE_BLOCK", "MANUAL_JAIL", "SUGGEST_BREAK"
    action VARCHAR(100) NOT NULL,      -- "BLOCK_REDDIT", "KILL_DISCORD", etc.
    target VARCHAR(255),               -- Site URL or process name
    
    -- Prediction context
    predicted_risk FLOAT NOT NULL,
    contributing_factors JSONB,        -- ["Low typing speed", "3 switches"]
    
    -- User feedback
    user_response VARCHAR(20),         -- "ACCEPTED", "REJECTED", "SNOOZED", "TIMEOUT"
    response_time_ms INT,              -- How long to respond
    
    -- Effectiveness (computed post-hoc)
    effectiveness_score FLOAT,         -- Did focus improve? (0-1)
    focus_score_before FLOAT,
    focus_score_after_5min FLOAT,
    
    -- A/B testing
    model_version VARCHAR(50),
    ab_test_group VARCHAR(50),
    
    created_at TIMESTAMPTZ DEFAULT NOW(),
    INDEX idx_interventions_session (session_id),
    INDEX idx_interventions_triggered (triggered_at DESC)
);

-- ========== Model Performance Tracking ==========
CREATE TABLE model_predictions (
    id BIGSERIAL PRIMARY KEY,
    session_id BIGINT REFERENCES sessions(id) ON DELETE CASCADE,
    
    -- Prediction
    predicted_at TIMESTAMPTZ NOT NULL,
    focus_score FLOAT NOT NULL,
    distraction_risk FLOAT NOT NULL,
    
    -- Ground truth (labeled later)
    actual_outcome VARCHAR(20),        -- "FOCUSED", "DISTRACTED", "PSEUDO_PRODUCTIVE"
    labeled_at TIMESTAMPTZ,
    label_source VARCHAR(20),          -- "TIMER", "HOTKEY", "RETROSPECTIVE"
    
    -- Model metadata
    model_version VARCHAR(50),
    inference_latency_ms INT,
    
    created_at TIMESTAMPTZ DEFAULT NOW(),
    INDEX idx_predictions_session (session_id),
    INDEX idx_predictions_time (predicted_at DESC)
);

-- ========== Analytics Views ==========
-- Daily summary
CREATE VIEW daily_stats AS
SELECT 
    user_id,
    DATE(start_time) as date,
    COUNT(*) as total_sessions,
    SUM(CASE WHEN completed THEN 1 ELSE 0 END) as completed_sessions,
    SUM(duration_seconds) / 3600.0 as total_hours,
    AVG(focus_score_avg) as avg_focus_score,
    SUM(context_switches) as total_switches,
    SUM(interventions_triggered) as total_interventions
FROM sessions
GROUP BY user_id, DATE(start_time);

-- Category breakdown
CREATE VIEW category_stats AS
SELECT 
    user_id,
    DATE(start_time) as date,
    category,
    COUNT(*) as session_count,
    SUM(duration_seconds) / 3600.0 as hours,
    AVG(focus_score_avg) as avg_focus_score
FROM sessions
WHERE category IS NOT NULL
GROUP BY user_id, DATE(start_time), category;
```

**Educational Note: TimescaleDB Hypertables**

Regular PostgreSQL table:
- All rows in one table
- Queries scan entire table (slow for large datasets)

TimescaleDB hypertable:
- Automatically partitioned by time (e.g., one chunk per day)
- Queries only scan relevant time range
- Old data automatically compressed/archived

Example:
```sql
SELECT * FROM events WHERE time > NOW() - INTERVAL '1 hour';
-- Only scans today's partition, ignores 89 days of old data
-- 100x faster than scanning everything
```

---

## API Schemas (REST/WebSocket)

### REST Endpoints

```typescript
// frontend/src/types/api.ts

// POST /api/v1/sessions
export interface CreateSessionRequest {
  type: "pomodoro" | "deep_work" | "natural";
  duration_minutes?: number;  // Optional override
  jail_enabled?: boolean;
}

export interface SessionResponse {
  id: number;
  user_id: number;
  start_time: string;  // ISO 8601
  end_time?: string;
  session_type: string;
  jail_enabled: boolean;
  focus_score_avg?: number;
  context_switches: number;
}

// GET /api/v1/analytics/daily
export interface DailyStatsRequest {
  date: string;  // YYYY-MM-DD
}

export interface DailyStatsResponse {
  date: string;
  total_sessions: number;
  completed_sessions: number;
  total_hours: number;
  avg_focus_score: number;
  category_breakdown: {
    [category: string]: {
      hours: number;
      focus_score: number;
    };
  };
}

// POST /api/v1/interventions/feedback
export interface InterventionFeedbackRequest {
  intervention_id: number;
  response: "accept" | "reject" | "snooze";
  response_time_ms: number;
}
```

### WebSocket Messages

```typescript
// frontend/src/types/websocket.ts

// ========== Server → Client (Push) ==========

export type ServerMessage =
  | ActivityUpdateMessage
  | PredictionMessage
  | InterventionMessage
  | SessionEndedMessage;

export interface ActivityUpdateMessage {
  type: "ACTIVITY_UPDATE";
  data: {
    timestamp: string;
    app_name: string;
    window_title: string;
    category: string;
    is_pseudo_productive: boolean;
  };
}

export interface PredictionMessage {
  type: "PREDICTION";
  data: {
    timestamp: string;
    focus_score: number;        // 0-100
    distraction_risk: number;   // 0-1
    risk_category: "LOW" | "MEDIUM" | "HIGH";
    confidence: number;
    contributing_factors: string[];
  };
}

export interface InterventionMessage {
  type: "INTERVENTION";
  data: {
    id: number;
    timestamp: string;
    action: string;             // "BLOCK_REDDIT", etc.
    reason: string;
    predicted_risk: number;
    timeout_seconds: number;    // Auto-accept after this
  };
}

export interface SessionEndedMessage {
  type: "SESSION_ENDED";
  data: {
    session_id: number;
    reason: "timer_expired" | "user_ended" | "system_error";
    summary: {
      duration_seconds: number;
      focus_score_avg: number;
      context_switches: number;
      interventions: number;
    };
  };
}

// ========== Client → Server (Actions) ==========

export type ClientMessage =
  | FeedbackMessage
  | StartSessionMessage
  | EndSessionMessage;

export interface FeedbackMessage {
  type: "INTERVENTION_FEEDBACK";
  data: {
    intervention_id: number;
    response: "accept" | "reject" | "snooze";
  };
}

export interface StartSessionMessage {
  type: "START_SESSION";
  data: {
    type: "pomodoro" | "deep_work";
  };
}

export interface EndSessionMessage {
  type: "END_SESSION";
  data: {
    session_id: number;
  };
}
```

---

## Summary: Schema Design Principles

1. **Fixed-size binary structs** (C++) for performance
2. **Type-safe dataclasses** (Python) for ML pipeline
3. **Normalized relational schema** (PostgreSQL) for business data
4. **Time-series hypertables** (TimescaleDB) for events
5. **JSONB for flexibility** (features, settings) where schema evolves
6. **Strongly-typed TypeScript** (frontend) for compile-time safety

Each layer uses the appropriate data model for its needs. The key is **clean interfaces** between layers:
- C++ writes binary → Python reads binary (schema contract)
- Python writes predictions → PostgreSQL stores (API contract)
- PostgreSQL serves data → TypeScript consumes (REST contract)

**Next:** We'll implement the C++ event engine using these schemas.
