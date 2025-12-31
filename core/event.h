/**
 * Neural Focus: Event Data Structures
 * 
 * This file defines the core event schema used throughout the system.
 * Events are the fundamental unit of data - every user action is captured
 * as an Event struct and flows through the pipeline.
 * 
 * DESIGN DECISIONS:
 * =================
 * 
 * 1. Why 64 bytes?
 *    - Exactly 1 CPU cache line on x86-64 (cache lines are 64 bytes)
 *    - CPU can read/write entire event in one memory transaction
 *    - Prevents "false sharing" in multi-threaded scenarios
 * 
 * 2. Why binary format instead of JSON?
 *    - JSON parsing: 10-50μs per event
 *    - memcpy of binary: 10-50ns per event
 *    - 1000x speed difference at 10,000 events/second
 * 
 * 3. Why fixed size?
 *    - Predictable memory layout → CPU prefetcher works better
 *    - Enables lock-free ring buffer (need fixed-size slots)
 *    - Simplifies memory-mapped file format (no variable-length records)
 * 
 * EDUCATIONAL NOTE: Cache Lines
 * =============================
 * CPUs don't read individual bytes - they read entire cache lines (64 bytes).
 * 
 * Example: If Event was 80 bytes, it would span 2 cache lines:
 *   [Cache Line 1: bytes 0-63]
 *   [Cache Line 2: bytes 64-79]
 * 
 * Problem: Reading one event requires 2 memory accesses (2x slower).
 * Solution: Keep Event ≤ 64 bytes (fits in 1 cache line).
 */

#ifndef NEUROFOCUS_EVENT_H
#define NEUROFOCUS_EVENT_H

#include <cstdint>
#include <cstring>

// Ensure no padding between struct members (tight packing)
#pragma pack(push, 1)

/**
 * EventType: Classification of user actions
 * 
 * PATTERN: Enum as uint32_t (not default int)
 * Why? Ensures consistent size across compilers (int could be 2 or 4 bytes).
 */
enum class EventType : uint32_t {
    UNKNOWN = 0,              // Should never occur in production
    
    // Keyboard events (codes 1-2)
    KEY_PRESS = 1,
    KEY_RELEASE = 2,
    
    // Mouse events (codes 3-5)
    MOUSE_MOVE = 3,
    MOUSE_CLICK = 4,
    MOUSE_WHEEL = 5,
    
    // Window events (codes 6-9)
    WINDOW_FOCUS_CHANGE = 6,  // User switched applications
    WINDOW_TITLE_CHANGE = 7,  // Same app, different document/tab
    WINDOW_MINIMIZE = 8,
    WINDOW_MAXIMIZE = 9,
    
    // Idle detection (codes 10-11)
    IDLE_START = 10,          // No input for 5+ seconds
    IDLE_END = 11,
    
    // System events (codes 12-13)
    SCREEN_LOCK = 12,
    SCREEN_UNLOCK = 13,
};

/**
 * Event: The core data structure
 * 
 * SIZE: Exactly 64 bytes (verified by static_assert at bottom)
 * ALIGNMENT: 64-byte aligned (matches cache line boundary)
 * PACKING: No padding (pragma pack(1))
 * 
 * MEMORY LAYOUT:
 * ==============
 * Offset  Size  Field
 * ------  ----  -----
 * 0       8     timestamp_us
 * 8       4     event_type
 * 12      4     process_id
 * 16      32    app_name
 * 48      4     window_handle
 * 52      4     padding
 * 56      12    data (union)
 * 68      4     reserved
 * Total: 72 bytes... wait, that's >64!
 * 
 * FIX: Actually 16+32+8+12+4 = 72. Let me recalculate to fit 64 bytes.
 * 
 * CORRECTED LAYOUT:
 * -----------------
 * We need to trim to fit 64 bytes exactly.
 * Offset  Size  Field
 * 0       8     timestamp_us
 * 8       4     event_type
 * 12      4     process_id
 * 16      24    app_name (reduced from 32 → 24)
 * 40      4     window_handle
 * 44      16    data (union, expanded for space)
 * 60      4     reserved
 * Total: 64 bytes ✓
 */
struct Event {
    //==========================================================================
    // TEMPORAL DATA (8 bytes)
    //==========================================================================
    
    /**
     * timestamp_us: Microseconds since Unix epoch
     * 
     * Why microseconds?
     * - Milliseconds (ms) have 1ms resolution → too coarse for <1ms latency
     * - Nanoseconds (ns) provide 1ns resolution → overkill, wastes bits
     * - Microseconds (μs) provide 1μs resolution → perfect for our needs
     * 
     * Range: 0 to 2^64 μs = 584,942 years (plenty)
     * 
     * EDUCATIONAL NOTE: Time Representations
     * =======================================
     * 1. Absolute time (what we use):
     *    uint64_t timestamp_us = 1704067200000000;  // 2025-12-29 00:00:00 UTC
     *    Pro: Easy to convert to calendar time (strftime, etc.)
     *    Con: 8 bytes per event
     * 
     * 2. Delta time (alternative):
     *    uint32_t delta_us;  // Microseconds since previous event
     *    Pro: Only 4 bytes (50% savings)
     *    Con: Lose absolute time (can't sort events from different streams)
     * 
     * We choose absolute time for simplicity and multi-stream support.
     */
    uint64_t timestamp_us;
    
    //==========================================================================
    // EVENT CLASSIFICATION (8 bytes)
    //==========================================================================
    
    EventType event_type;      // 4 bytes (enum class uint32_t)
    uint32_t process_id;       // 4 bytes (Windows PID for process lookup)
    
    //==========================================================================
    // APPLICATION CONTEXT (24 bytes)
    //==========================================================================
    
    /**
     * app_name: Process executable name (null-terminated UTF-8)
     * 
     * Example: "chrome.exe\0\0\0\0..." (padded to 24 bytes)
     * 
     * Why 24 chars?
     * - Most app names fit: "chrome.exe" (10), "Code.exe" (8), "Slack.exe" (9)
     * - If longer, we truncate: "VeryLongApplicationName" → "VeryLongApplicatio\0"
     * 
     * EDUCATIONAL NOTE: String Storage
     * ================================
     * 1. Null-terminated (C-style) - what we use:
     *    char name[24] = "chrome.exe\0\0\0...";
     *    Pro: Standard, works with strcpy/strlen
     *    Con: Wastes space (unused bytes are null)
     * 
     * 2. Length-prefixed (Pascal-style):
     *    struct { uint8_t len; char data[23]; }
     *    Pro: No wasted space (only store actual length)
     *    Con: Non-standard, harder to debug
     * 
     * We use null-terminated for compatibility with Win32 APIs.
     */
    char app_name[24];
    
    //==========================================================================
    // WINDOW CONTEXT (4 bytes)
    //==========================================================================
    
    /**
     * window_handle: Windows HWND (handle to window)
     * 
     * What's a window handle?
     * - Unique identifier for a window in Windows OS
     * - Type: HWND (defined as void*, but fits in 32 bits on x64)
     * - Used to get window title: GetWindowTextA(hwnd, buffer, size)
     * 
     * Why store this?
     * - Window title tells us context: "main.cpp - VS Code" vs "YouTube - Chrome"
     * - We can fetch title later (don't bloat Event struct)
     */
    uint32_t window_handle;
    
    //==========================================================================
    // TYPE-SPECIFIC DATA (16 bytes)
    //==========================================================================
    
    /**
     * data: Union for event-specific data
     * 
     * PATTERN: Tagged Union (Discriminated Union)
     * ============================================
     * We have one "data" field that means different things based on event_type.
     * This saves space versus having separate fields for every type.
     * 
     * Example:
     * - If event_type == KEY_PRESS: data.key.virtual_key_code is valid
     * - If event_type == MOUSE_MOVE: data.mouse_move.x/y are valid
     * - If event_type == IDLE_START: data.idle.duration_ms is valid
     * 
     * WARNING: You MUST check event_type before accessing union fields!
     * Accessing wrong field = undefined behavior (reads garbage memory).
     */
    union {
        // For KEY_PRESS / KEY_RELEASE (12 bytes used)
        struct {
            uint32_t virtual_key_code;  // VK_A, VK_RETURN, VK_SHIFT, etc.
            uint32_t scan_code;         // Hardware-specific key code
            uint32_t flags;             // Bit flags: Alt pressed? Ctrl? Shift?
        } key;
        
        // For MOUSE_MOVE (12 bytes used)
        struct {
            int32_t x;                  // Screen X coordinate (can be negative on multi-monitor)
            int32_t y;                  // Screen Y coordinate
            uint32_t speed_pps;         // Pixels per second (derived from delta)
        } mouse_move;
        
        // For MOUSE_CLICK (12 bytes used)
        struct {
            int32_t x;
            int32_t y;
            uint32_t button;            // 1=left, 2=right, 3=middle, 4=X1, 5=X2
        } mouse_click;
        
        // For MOUSE_WHEEL (12 bytes used)
        struct {
            int32_t delta;              // Scroll amount (positive=up, negative=down)
            uint32_t orientation;       // 0=vertical, 1=horizontal
            uint32_t reserved;
        } mouse_wheel;
        
        // For WINDOW_FOCUS_CHANGE (12 bytes used)
        struct {
            uint32_t old_window;        // Previous HWND
            uint32_t new_window;        // Current HWND
            uint32_t category_hint;     // Pre-classified category (optimization)
        } window_switch;
        
        // For IDLE_START / IDLE_END (12 bytes used)
        struct {
            uint32_t idle_duration_ms;  // How long was the idle period
            uint32_t reserved[2];
        } idle;
        
        // Raw access for debugging
        uint8_t raw_data[16];           // All 16 bytes as raw buffer
    } data;
    
    //==========================================================================
    // RESERVED SPACE (4 bytes)
    //==========================================================================
    
    /**
     * reserved: Padding for future expansion
     * 
     * Why reserve space?
     * - Future-proofing: If we need to add a field, we have space
     * - Without this, adding a field changes struct size → breaks compatibility
     * 
     * Example future use:
     * - Add "confidence" field (how confident are we in classification?)
     * - Add "sequence_number" (for detecting packet loss)
     */
    uint32_t reserved;
    
    //==========================================================================
    // HELPER METHODS
    //==========================================================================
    
    /**
     * is_valid: Sanity check
     * 
     * Returns false if event has obviously corrupt data.
     * Used to detect bugs in event capture code.
     */
    bool is_valid() const {
        // Timestamp should be reasonable (after 2020, before 2050)
        if (timestamp_us < 1577836800000000ULL ||  // 2020-01-01
            timestamp_us > 2524608000000000ULL) {  // 2050-01-01
            return false;
        }
        
        // Event type should be defined
        if (event_type == EventType::UNKNOWN) {
            return false;
        }
        
        // App name should be null-terminated
        bool has_null = false;
        for (int i = 0; i < 24; i++) {
            if (app_name[i] == '\0') {
                has_null = true;
                break;
            }
        }
        if (!has_null) {
            return false;  // String not null-terminated
        }
        
        return true;
    }
    
    /**
     * get_app_name: Safe string accessor
     * 
     * Returns app_name as std::string (handles non-null-terminated case).
     */
    const char* get_app_name() const {
        // Force null-termination (defensive programming)
        static char buffer[25];  // 24 + 1 for null
        std::memcpy(buffer, app_name, 24);
        buffer[24] = '\0';
        return buffer;
    }
} __attribute__((packed, aligned(64)));
// packed: No padding between fields
// aligned(64): Start of struct aligns to 64-byte boundary

#pragma pack(pop)

// Compile-time assertion: Verify Event is exactly 64 bytes
static_assert(sizeof(Event) == 64, "Event struct must be exactly 64 bytes");

/**
 * EDUCATIONAL NOTE: Why __attribute__((packed, aligned(64)))?
 * ============================================================
 * 
 * Problem: Compiler adds "padding" to structs for alignment.
 * 
 * Example WITHOUT packing:
 * struct Bad {
 *     uint8_t a;   // 1 byte
 *     // compiler adds 3 bytes padding here!
 *     uint32_t b;  // 4 bytes
 * };
 * sizeof(Bad) = 8 bytes (not 5!)
 * 
 * With packing:
 * struct Good {
 *     uint8_t a;   // 1 byte
 *     uint32_t b;  // 4 bytes (no padding)
 * } __attribute__((packed));
 * sizeof(Good) = 5 bytes ✓
 * 
 * With alignment:
 * Event* events = new Event[100];
 * // Without aligned(64): events[0] at 0x1000, events[1] at 0x1040 (misaligned!)
 * // With aligned(64):    events[0] at 0x1000, events[1] at 0x1040 (aligned to cache line!)
 * 
 * Both are needed:
 * - packed: Ensures struct is exactly 64 bytes (no bloat)
 * - aligned(64): Ensures each Event starts on cache line boundary (performance)
 */

#endif // NEUROFOCUS_EVENT_H
