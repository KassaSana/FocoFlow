# Context Recovery System: Technical Design

**Feature:** "Where Was I?" - Automatic work context recovery after distractions

---

## The Problem

When users return from a distraction (Twitter, Discord, bathroom break), they experience **context amnesia**:

```
Before Distraction:
- Editing main.py, line 234
- Fixing a bug in the calculate_total() function
- Had a mental model of the algorithm in working memory

After 5-minute Twitter break:
- "Wait, what was I doing?"
- "Which file was I in?"
- "What was I trying to fix?"
- Takes 10-15 minutes to rebuild mental context
```

**Research shows:** It takes an average of **23 minutes** to fully recover context after an interruption (UC Irvine study).

---

## The Solution

**Context Snapshots:** Continuously capture what the user is working on.

**Distraction Detection:** Identify when user leaves productive work for distracting apps.

**Context Recovery Overlay:** When user returns, show a brief overlay:

```
┌─────────────────────────────────────────────────────────────────┐
│  🔄 Welcome Back! Here's what you were doing:                   │
│                                                                  │
│  📁 File: src/services/analytics.py                             │
│  📍 Line: 234 (function: calculate_total)                       │
│  ⏱️  You were focused for: 23 minutes                           │
│  🎯 Context: Building > Backend API                             │
│                                                                  │
│  Recent activity:                                                │
│  • Edited calculate_total() function                            │
│  • Searched "python decimal precision"                          │
│  • Viewed tests/test_analytics.py                               │
│                                                                  │
│  [Got it! (Enter)] [Show more history (H)]      Auto-dismiss: 5s│
└─────────────────────────────────────────────────────────────────┘
```

---

## System Architecture

### Data Flow

```
┌─────────────────────────────────────────────────────────────────┐
│                    Context Recovery System                       │
│                                                                  │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────┐      │
│  │   Context    │    │  Distraction │    │   Recovery   │      │
│  │  Snapshotter │───▶│   Detector   │───▶│   Overlay    │      │
│  └──────────────┘    └──────────────┘    └──────────────┘      │
│         │                   │                    │               │
│         ▼                   ▼                    ▼               │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────┐      │
│  │  Snapshot    │    │  Distraction │    │   Overlay    │      │
│  │   Buffer     │    │    State     │    │   Window     │      │
│  │ (last 10min) │    │   Machine    │    │  (Win32/Qt)  │      │
│  └──────────────┘    └──────────────┘    └──────────────┘      │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

### State Machine: Distraction Tracker

```
                         ┌──────────────────┐
                         │                  │
              ┌─────────▶│     FOCUSED      │◀─────────┐
              │          │  (productive app)│          │
              │          └────────┬─────────┘          │
              │                   │                    │
              │   Return to       │ Switch to          │ Overlay
              │   productive      │ distraction        │ dismissed
              │   app             │                    │
              │                   ▼                    │
              │          ┌──────────────────┐          │
              │          │                  │          │
              │          │   DISTRACTED     │          │
              │          │ (non-productive) │          │
              │          └────────┬─────────┘          │
              │                   │                    │
              │                   │ Return to          │
              │                   │ productive app     │
              │                   ▼                    │
              │          ┌──────────────────┐          │
              │          │                  │          │
              └──────────│   RECOVERING     │──────────┘
                         │ (showing overlay)│
                         └──────────────────┘

States:
- FOCUSED: User in productive app (IDE, docs, etc.)
- DISTRACTED: User in distracting app (social media, games, etc.)
- RECOVERING: User returned, showing context overlay

Transitions:
- FOCUSED → DISTRACTED: Window switch to non-productive app
- DISTRACTED → RECOVERING: Window switch back to productive app
- RECOVERING → FOCUSED: Overlay dismissed (timeout or keypress)
```

---

## Data Structures

### Context Snapshot

```cpp
/**
 * ContextSnapshot: A point-in-time capture of user's work context
 * 
 * Captured every 30 seconds while user is in FOCUSED state.
 * Stored in circular buffer (last 10 minutes = 20 snapshots).
 */
struct ContextSnapshot {
    // Timing
    uint64_t timestamp_us;           // When snapshot was taken
    uint32_t duration_seconds;       // How long in this context
    
    // Application context
    char app_name[32];               // "Code.exe"
    char window_title[256];          // "main.py - Visual Studio Code"
    uint32_t process_id;
    
    // IDE-specific context (if detectable)
    char file_path[260];             // "C:\project\src\main.py"
    uint32_t line_number;            // 234 (from window title parsing)
    char function_name[64];          // "calculate_total" (if detectable)
    
    // Activity metrics
    uint32_t keystrokes_in_context;  // Total keystrokes in this context
    uint32_t mouse_clicks;           // Click count
    float typing_speed_avg;          // Average typing speed
    
    // Category
    uint8_t category;                // 0=Building, 1=Studying, etc.
    uint8_t is_productive;           // Was this productive time?
    
    // Search/browser context (if applicable)
    char last_search_query[128];     // "python decimal precision"
    char browser_url_domain[64];     // "stackoverflow.com" (not full URL for privacy)
};
```

### Context History (Circular Buffer)

```cpp
/**
 * ContextHistory: Rolling history of recent context snapshots
 * 
 * Design Pattern: Circular Buffer
 * - Fixed memory (20 snapshots * ~800 bytes = 16KB)
 * - Old snapshots automatically evicted
 * - O(1) insert, O(1) access to recent contexts
 */
class ContextHistory {
    static constexpr size_t MAX_SNAPSHOTS = 20;  // 10 minutes @ 30s interval
    
    ContextSnapshot snapshots_[MAX_SNAPSHOTS];
    size_t head_ = 0;                             // Next write position
    size_t count_ = 0;                            // Valid snapshots
    
public:
    void push(const ContextSnapshot& snapshot);
    
    // Get most recent snapshot (what user was doing before distraction)
    const ContextSnapshot& get_last() const;
    
    // Get last N snapshots (for "recent activity" display)
    std::vector<ContextSnapshot> get_recent(size_t n) const;
    
    // Find snapshot by app name (e.g., "what was I doing in VS Code?")
    const ContextSnapshot* find_by_app(const char* app_name) const;
};
```

### Recovery Display Data

```cpp
/**
 * RecoveryContext: Data prepared for the overlay display
 * 
 * Compiled from ContextHistory when user returns from distraction.
 */
struct RecoveryContext {
    // Primary context (what they were doing)
    ContextSnapshot primary;
    
    // Distraction info
    uint64_t distraction_start_us;   // When they got distracted
    uint64_t distraction_end_us;     // When they returned
    uint32_t distraction_duration_s; // How long away
    char distraction_app[32];        // What distracted them ("chrome.exe")
    
    // Recent activity summary
    struct RecentActivity {
        char description[128];       // "Edited calculate_total() function"
        uint64_t timestamp_us;
    };
    RecentActivity recent_activities[5];  // Last 5 notable actions
    
    // Stats
    uint32_t focus_duration_before;  // How long were they focused?
    uint32_t context_switches_today; // How many distractions today?
};
```

---

## Implementation Details

### 1. Context Capture (C++)

**Where:** Integrated into event_processor.cpp

**How:** Every 30 seconds while in FOCUSED state:
1. Get foreground window (GetForegroundWindow)
2. Get window title (GetWindowText)
3. Parse title for IDE-specific info (file, line number)
4. Compute activity metrics (keystrokes, mouse clicks)
5. Push snapshot to ContextHistory

```cpp
// Example: Parse VS Code window title
// Input:  "main.py - src - MyProject - Visual Studio Code"
// Output: file_path="main.py", project="MyProject"

bool parse_vscode_title(const char* title, ContextSnapshot& snapshot) {
    // Pattern: "<file> - <folder> - <project> - Visual Studio Code"
    // or: "<file>:<line> - <folder> - <project> - Visual Studio Code"
    
    // Extract file name (before first " - ")
    const char* dash = strstr(title, " - ");
    if (!dash) return false;
    
    size_t file_len = dash - title;
    if (file_len >= sizeof(snapshot.file_path)) return false;
    
    strncpy(snapshot.file_path, title, file_len);
    snapshot.file_path[file_len] = '\0';
    
    // Check for line number (file.py:123)
    const char* colon = strchr(snapshot.file_path, ':');
    if (colon) {
        snapshot.line_number = atoi(colon + 1);
        // Remove line number from file path
        *const_cast<char*>(colon) = '\0';
    }
    
    return true;
}
```

### 2. Distraction Detection (C++)

**Trigger:** WINDOW_FOCUS_CHANGE event

**Logic:**
```cpp
void on_window_focus_change(const Event& event) {
    const char* new_app = event.get_app_name();
    
    bool was_productive = is_productive_app(previous_app_);
    bool is_productive = is_productive_app(new_app);
    
    if (was_productive && !is_productive) {
        // FOCUSED → DISTRACTED
        distraction_start_time_ = event.timestamp_us;
        last_productive_context_ = context_history_.get_last();
        state_ = DistractionState::DISTRACTED;
    }
    else if (!was_productive && is_productive) {
        // DISTRACTED → RECOVERING
        if (state_ == DistractionState::DISTRACTED) {
            uint64_t distraction_duration = event.timestamp_us - distraction_start_time_;
            
            // Only show overlay if distraction was >30 seconds
            // (brief glances at notifications don't need recovery)
            if (distraction_duration > 30 * 1000000) {
                trigger_recovery_overlay(last_productive_context_, distraction_duration);
                state_ = DistractionState::RECOVERING;
            } else {
                state_ = DistractionState::FOCUSED;
            }
        }
    }
    
    previous_app_ = new_app;
}
```

### 3. Recovery Overlay (Win32 + Optional Qt/Electron)

**Option A: Native Win32 Overlay (Lightweight)**

```cpp
/**
 * Create a transparent overlay window that shows context recovery info.
 * 
 * Window properties:
 * - WS_EX_TOPMOST: Always on top
 * - WS_EX_LAYERED: Support transparency
 * - WS_EX_NOACTIVATE: Don't steal focus from user's app
 * - WS_POPUP: No title bar or borders
 */
HWND create_recovery_overlay(const RecoveryContext& ctx) {
    // Register window class
    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = OverlayWndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = L"NeuralFocusOverlay";
    wc.hbrBackground = NULL;  // Transparent background
    RegisterClassEx(&wc);
    
    // Create overlay window (positioned at top-right of screen)
    int screen_width = GetSystemMetrics(SM_CXSCREEN);
    int overlay_width = 450;
    int overlay_height = 280;
    int x = screen_width - overlay_width - 20;  // 20px margin from right
    int y = 20;  // 20px margin from top
    
    HWND hwnd = CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
        L"NeuralFocusOverlay",
        L"Context Recovery",
        WS_POPUP,
        x, y, overlay_width, overlay_height,
        NULL, NULL, GetModuleHandle(NULL), NULL
    );
    
    // Set transparency (90% opaque)
    SetLayeredWindowAttributes(hwnd, 0, 230, LWA_ALPHA);
    
    // Store context data for painting
    SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&ctx));
    
    // Show with fade-in animation
    AnimateWindow(hwnd, 200, AW_BLEND);
    
    // Auto-dismiss timer (5 seconds)
    SetTimer(hwnd, TIMER_AUTODISMISS, 5000, NULL);
    
    return hwnd;
}
```

**Option B: Electron Overlay (Rich UI)**

If using Electron for the frontend dashboard, we can create a more polished overlay:

```typescript
// frontend/src/overlay/RecoveryOverlay.tsx

interface RecoveryContext {
  filePath: string;
  lineNumber: number;
  appName: string;
  focusDuration: number;
  distractionDuration: number;
  recentActivities: string[];
}

const RecoveryOverlay: React.FC<{ context: RecoveryContext }> = ({ context }) => {
  const [countdown, setCountdown] = useState(5);
  
  useEffect(() => {
    const timer = setInterval(() => {
      setCountdown(c => {
        if (c <= 1) {
          window.close();  // Auto-dismiss
          return 0;
        }
        return c - 1;
      });
    }, 1000);
    
    // Dismiss on any keypress
    const handleKeyPress = () => window.close();
    window.addEventListener('keydown', handleKeyPress);
    
    return () => {
      clearInterval(timer);
      window.removeEventListener('keydown', handleKeyPress);
    };
  }, []);
  
  return (
    <div className="recovery-overlay">
      <h2>🔄 Welcome Back!</h2>
      <p>Here's what you were doing:</p>
      
      <div className="context-card">
        <div className="file-info">
          <span className="icon">📁</span>
          <span className="path">{context.filePath}</span>
          {context.lineNumber > 0 && (
            <span className="line">Line {context.lineNumber}</span>
          )}
        </div>
        
        <div className="stats">
          <span>⏱️ Focused for: {formatDuration(context.focusDuration)}</span>
          <span>😵 Away for: {formatDuration(context.distractionDuration)}</span>
        </div>
      </div>
      
      {context.recentActivities.length > 0 && (
        <div className="recent-activity">
          <h3>Recent activity:</h3>
          <ul>
            {context.recentActivities.map((activity, i) => (
              <li key={i}>{activity}</li>
            ))}
          </ul>
        </div>
      )}
      
      <div className="footer">
        <button onClick={() => window.close()}>Got it! (Enter)</button>
        <span className="countdown">Auto-dismiss: {countdown}s</span>
      </div>
    </div>
  );
};
```

### 4. IDE-Specific Integration

For richer context capture, we can integrate with specific IDEs:

**VS Code Extension (Future Enhancement):**

```typescript
// vscode-extension/src/extension.ts

import * as vscode from 'vscode';

export function activate(context: vscode.ExtensionContext) {
    // Track active file and cursor position
    vscode.window.onDidChangeActiveTextEditor(editor => {
        if (editor) {
            sendContext({
                file: editor.document.fileName,
                line: editor.selection.active.line,
                language: editor.document.languageId,
                workspaceFolder: vscode.workspace.name
            });
        }
    });
    
    // Track cursor position changes
    vscode.window.onDidChangeTextEditorSelection(event => {
        sendContext({
            file: event.textEditor.document.fileName,
            line: event.selections[0].active.line,
            // Get function name at cursor
            functionName: getFunctionAtLine(event.textEditor)
        });
    });
}

function sendContext(ctx: any) {
    // Send to Neural Focus via local WebSocket
    const ws = new WebSocket('ws://localhost:5001/context');
    ws.send(JSON.stringify(ctx));
}
```

---

## Window Title Parsing Rules

Different apps encode context in window titles differently:

### VS Code
```
Pattern: "<file> - <folder> - <project> - Visual Studio Code"
Example: "main.py - src - MyProject - Visual Studio Code"
Extract: file=main.py, project=MyProject

With unsaved: "• main.py - src - MyProject - Visual Studio Code"
Extract: file=main.py, unsaved=true

With line: "main.py:234 - src - MyProject - Visual Studio Code"
Extract: file=main.py, line=234
```

### IntelliJ / PyCharm
```
Pattern: "<project> – <file> [<path>]"
Example: "MyProject – main.py [src/services]"
Extract: file=main.py, path=src/services, project=MyProject
```

### Chrome / Edge
```
Pattern: "<page_title> - <browser_name>"
Example: "Stack Overflow - Where Developers Learn - Google Chrome"
Extract: title="Stack Overflow - Where Developers Learn"

With domain hint: check if title contains "stackoverflow", "github", "docs"
```

### Terminal (Windows Terminal, cmd, PowerShell)
```
Pattern varies, but often includes current directory
Example: "Administrator: Windows PowerShell - C:\Projects\MyApp"
Extract: shell=PowerShell, directory=C:\Projects\MyApp
```

### Implementation

```cpp
struct ParsedWindowTitle {
    char app_type[32];       // "vscode", "pycharm", "chrome", "terminal"
    char file_path[260];
    uint32_t line_number;
    char project_name[64];
    char browser_domain[64];
    bool has_unsaved_changes;
};

ParsedWindowTitle parse_window_title(const char* app_name, const char* title) {
    ParsedWindowTitle result = {};
    
    if (strstr(app_name, "Code.exe") || strstr(app_name, "code")) {
        strcpy(result.app_type, "vscode");
        parse_vscode_title(title, result);
    }
    else if (strstr(app_name, "idea") || strstr(app_name, "pycharm")) {
        strcpy(result.app_type, "jetbrains");
        parse_jetbrains_title(title, result);
    }
    else if (strstr(app_name, "chrome") || strstr(app_name, "firefox") || strstr(app_name, "edge")) {
        strcpy(result.app_type, "browser");
        parse_browser_title(title, result);
    }
    else if (strstr(app_name, "WindowsTerminal") || strstr(app_name, "cmd") || strstr(app_name, "powershell")) {
        strcpy(result.app_type, "terminal");
        parse_terminal_title(title, result);
    }
    
    return result;
}
```

---

## Activity Description Generation

Convert raw events into human-readable activity descriptions:

```cpp
/**
 * Generate activity description from context changes.
 * 
 * Examples:
 * - "Edited main.py (45 keystrokes)"
 * - "Searched 'python async await'"
 * - "Viewed documentation on stackoverflow.com"
 * - "Ran terminal command"
 */
std::string describe_activity(const ContextSnapshot& prev, const ContextSnapshot& curr) {
    std::stringstream ss;
    
    // File change in IDE
    if (strcmp(prev.file_path, curr.file_path) != 0 && strlen(curr.file_path) > 0) {
        if (curr.keystrokes_in_context > 10) {
            ss << "Edited " << basename(curr.file_path) 
               << " (" << curr.keystrokes_in_context << " keystrokes)";
        } else {
            ss << "Viewed " << basename(curr.file_path);
        }
        return ss.str();
    }
    
    // Search query
    if (strlen(curr.last_search_query) > 0 && 
        strcmp(prev.last_search_query, curr.last_search_query) != 0) {
        ss << "Searched '" << curr.last_search_query << "'";
        return ss.str();
    }
    
    // Browser domain change
    if (strlen(curr.browser_url_domain) > 0 &&
        strcmp(prev.browser_url_domain, curr.browser_url_domain) != 0) {
        if (is_documentation_site(curr.browser_url_domain)) {
            ss << "Viewed documentation on " << curr.browser_url_domain;
        } else {
            ss << "Browsed " << curr.browser_url_domain;
        }
        return ss.str();
    }
    
    // Terminal activity
    if (strcmp(curr.app_type, "terminal") == 0 && curr.keystrokes_in_context > 5) {
        ss << "Ran terminal commands";
        return ss.str();
    }
    
    // Default: time spent in app
    ss << "Working in " << curr.app_name << " (" << curr.duration_seconds << "s)";
    return ss.str();
}
```

---

## Configuration Options

Users should be able to customize the recovery behavior:

```json
{
  "context_recovery": {
    "enabled": true,
    "min_distraction_seconds": 30,      // Don't show for brief glances
    "max_distraction_seconds": 3600,    // Don't show after 1 hour (context too stale)
    "overlay_duration_seconds": 5,      // Auto-dismiss after 5s
    "overlay_position": "top-right",    // top-right, top-left, center
    "show_recent_activities": true,
    "max_recent_activities": 5,
    "dismiss_on_keypress": true,
    "sound_enabled": false,             // Optional chime on return
    "ide_integration": {
      "vscode_extension": false,        // Enable richer VS Code tracking
      "parse_window_titles": true       // Parse titles for file/line info
    }
  }
}
```

---

## Privacy Considerations

**What we capture:**
- ✅ Window titles (contains file names, project names)
- ✅ App names (process names)
- ✅ Activity metrics (keystrokes count, not content)
- ✅ Browser domains (not full URLs)

**What we DON'T capture:**
- ❌ Keystroke content (what you typed)
- ❌ Full URLs (only domain: "github.com" not "github.com/secret-repo")
- ❌ File contents
- ❌ Screenshots

**Data retention:**
- Context snapshots kept for 10 minutes (in memory)
- No persistent logging of window titles by default
- User can opt-in to longer history for analytics

---

## Success Metrics

**Measure effectiveness:**

1. **Context Recovery Time:**
   - Baseline: How long to resume typing after return (without overlay)
   - With overlay: Same measurement
   - Target: 50% reduction in resume time

2. **User Satisfaction:**
   - Survey: "Did the overlay help you remember what you were doing?"
   - Target: >80% "Yes" responses

3. **Distraction Recovery Rate:**
   - How often users stay focused after returning (vs. getting distracted again)
   - Target: 20% improvement

4. **Overlay Engagement:**
   - Did user read it (waited for content) or dismiss immediately?
   - Target: >60% read before dismiss

---

## Implementation Roadmap

### Phase 1: Basic Context Capture (Week 1)
- [ ] Add ContextSnapshot struct to C++ codebase
- [ ] Implement ContextHistory circular buffer
- [ ] Capture snapshots every 30s during FOCUSED state
- [ ] Parse VS Code window titles for file/line info

### Phase 2: Distraction Detection (Week 1-2)
- [ ] Implement distraction state machine
- [ ] Track transitions between productive/distraction apps
- [ ] Detect "return from distraction" events
- [ ] Filter brief glances (<30s)

### Phase 3: Basic Overlay (Week 2)
- [ ] Create Win32 overlay window
- [ ] Display basic context (file, app, duration)
- [ ] Auto-dismiss after 5 seconds
- [ ] Dismiss on keypress

### Phase 4: Rich Context (Week 3)
- [ ] Parse window titles for multiple IDEs
- [ ] Generate activity descriptions
- [ ] Track recent activities (last 5 actions)
- [ ] Add browser domain tracking

### Phase 5: Polish (Week 4)
- [ ] Electron overlay (if using Electron frontend)
- [ ] Configuration options UI
- [ ] Analytics (track effectiveness)
- [ ] Optional VS Code extension

---

## Summary

The Context Recovery System addresses a real pain point for knowledge workers:
- **Captures** rich work context (file, line, project, recent actions)
- **Detects** when users return from distractions
- **Displays** a brief overlay to restore mental context
- **Respects** privacy (no content capture, local-only)
- **Configurable** (timing, position, content)

This feature differentiates Neural Focus from simple blockers—it doesn't just prevent distractions, it **helps you recover** when distractions happen.
