# Core C++ Event Engine

## What We've Built

### Completed Components ✅

1. **event.h** - Event data structure (64-byte cache-aligned)
2. **ring_buffer.h** - Lock-free queue for events
3. **windows_hooks.h/.cpp** - System event capture (keyboard, mouse, window focus)
4. **event_processor.h/.cpp** - Hooks -> ring buffer coordinator
5. **mmap_logger.h/.cpp** - Memory-mapped event log persistence
6. **zmq_publisher.h/.cpp** - ZeroMQ event publisher
7. **neurofocus_engine.cpp** - End-to-end capture + log + publish runner

### Experimental — not integrated into the engine binary

The `experimental/` subdirectory holds the context-recovery R&D. These headers
and the standalone demo do not link into `neurofocus_engine` and are not part
of the `neurofocus_core` static library. They build only when the optional
`context_demo` target is enabled.

1. **experimental/context.h** - Context recovery snapshot type
2. **experimental/title_parser.h** - Parse window titles to extract work context
3. **experimental/overlay.h** - Win32 overlay UI for context recovery
4. **experimental/context_tracker.h** - State machine for context recovery
5. **experimental/context_demo.cpp** - Standalone demo for the four headers above

## Windows Hooks (NEW!)

The `windows_hooks` module captures real-time system events using Win32 low-level hooks:

### Features
- **Keyboard capture** - Every keypress/release (WH_KEYBOARD_LL)
- **Mouse capture** - Clicks, movement, scrolling (WH_MOUSE_LL)
- **Window focus** - Detects when you switch apps (SetWinEventHook)
- **Non-blocking** - Runs on dedicated message thread
- **Thread-safe** - Can be used from any thread

### How to Use

```cpp
#include "windows_hooks.h"

// 1. Define callback for events
void my_callback(const Event& event) {
    printf("Got event: type=%d\n", event.event_type);
    // Push to queue, process later...
}

// 2. Configure hooks
hooks::HooksConfig config;
config.callback = my_callback;
config.capture_keyboard = true;
config.capture_mouse = true;
config.capture_window_focus = true;

// 3. Start capturing
hooks::HooksManager mgr;
if (!mgr.start(config)) {
    fprintf(stderr, "Failed to start hooks\n");
    return 1;
}

// 4. Events now flow to your callback!
// Do your work here...

// 5. Stop when done
mgr.stop();
```

### Testing

We've included a test program that prints captured events:

```bash
# Compile
g++ -std=c++17 hooks_test.cpp windows_hooks.cpp -o hooks_test.exe -luser32 -lgdi32 -lpsapi

# Run
./hooks_test.exe

# You'll see:
# [HooksManager] Started successfully
# Hooks are active!
# - Type on keyboard
# - Click mouse
# - Switch windows
# - Press Ctrl+C to stop
#
# [     10] KEY_PRESS   vk=65  app=Code.exe
# [     20] MOUSE_CLICK btn=1 pos=(1234,567) app=chrome.exe
# [     30] WIN_FOCUS   app=WindowsTerminal.exe
```

### Architecture

```
User Action (keyboard/mouse)
    ↓
Windows sends to low-level hook
    ↓
windows_hooks.cpp callback
    ↓
Create Event struct
    ↓
Call your callback(event)
    ↓
You push to queue for processing
```

**Performance:** Hook callbacks run on Windows' hook thread. They MUST be fast (<100μs) to avoid blocking the system. That's why we just create an Event and push to a queue - all the heavy processing happens later on another thread.

### Design Decisions

**Q: Why a dedicated message thread?**
A: Windows hooks require a message loop. We run `GetMessage()` on a background thread so the main thread stays responsive.

**Q: Why use callbacks instead of returning events directly?**
A: Hooks are asynchronous - events arrive whenever the user acts. A callback pattern fits this perfectly.

**Q: Why singleton instance?**
A: Windows hook callbacks are static functions (C calling convention). We need a way to get back to our C++ object instance - hence the singleton pattern.

## What's Next?

Now that we can capture, log, publish, and build, the next step is:

1. **Latency Profiling** - Measure p50/p99/p999 and stress tests

Each step builds on what we have. We're making steady progress! 🚀

## Building Everything

```bash
# Test context recovery (experimental, run from core/)
g++ -std=c++17 -I. experimental/context_demo.cpp -o context_demo.exe -luser32 -lgdi32
./context_demo.exe --visual

# Test hooks
g++ -std=c++17 hooks_test.cpp windows_hooks.cpp -o hooks_test.exe -luser32 -lgdi32 -lpsapi
./hooks_test.exe

# Test memory-mapped logger
g++ -std=c++17 mmap_logger_test.cpp mmap_logger.cpp -o mmap_logger_test.exe
./mmap_logger_test.exe

# Test ZeroMQ publisher
g++ -std=c++17 zmq_publisher_test.cpp zmq_publisher.cpp -o zmq_publisher_test.exe -lzmq
./zmq_publisher_test.exe

# Build the live engine (requires ZeroMQ headers/libs)
g++ -std=c++17 neurofocus_engine.cpp event_processor.cpp windows_hooks.cpp \
    mmap_logger.cpp zmq_publisher.cpp -o neurofocus_engine.exe -luser32 -lgdi32 -lpsapi -lzmq
./neurofocus_engine.exe --log-dir . --log-base events
```
