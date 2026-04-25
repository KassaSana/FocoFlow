/**
 * Neural Focus: Context Snapshot Data Structures
 * 
 * This file defines the data structures for capturing work context.
 * Context snapshots are taken periodically while the user is focused,
 * allowing us to show "where you were" when they return from distractions.
 * 
 * THE PROBLEM WE'RE SOLVING:
 * ==========================
 * 
 * When you get distracted (check Twitter, respond to Slack), you lose
 * your mental context:
 * - What file were you editing?
 * - What line were you on?
 * - What were you trying to fix?
 * 
 * This is called "context amnesia" and research shows it takes an average
 * of 23 MINUTES to fully rebuild mental context after an interruption.
 * 
 * OUR SOLUTION:
 * =============
 * 
 * 1. Continuously capture "context snapshots" while you're working
 * 2. Detect when you get distracted (switch to non-productive app)
 * 3. Detect when you return (switch back to productive app)
 * 4. Show a brief overlay: "You were editing main.py line 234"
 * 
 * DESIGN DECISIONS:
 * =================
 * 
 * 1. Fixed-size strings (not std::string):
 *    - Predictable memory layout
 *    - Cache-friendly (no heap allocations)
 *    - Safe to copy with memcpy
 * 
 * 2. Circular buffer for history:
 *    - Fixed memory (no growth)
 *    - Old snapshots automatically evicted
 *    - O(1) operations
 * 
 * 3. Parse window titles for context:
 *    - VS Code: "main.py - src - Project - Visual Studio Code"
 *    - Extract: file=main.py, project=Project
 *    - No special IDE integration required
 */

#ifndef NEUROFOCUS_CONTEXT_H
#define NEUROFOCUS_CONTEXT_H

#include <cstdint>
#include <cstring>
#include <algorithm>

#pragma pack(push, 1)

/**
 * AppCategory: Classification of applications
 * 
 * Used to determine if user is in "productive" vs "distracting" context.
 */
enum class AppCategory : uint8_t {
    UNKNOWN = 0,
    
    // Productive categories
    IDE = 1,              // VS Code, IntelliJ, PyCharm, Visual Studio
    TERMINAL = 2,         // Windows Terminal, cmd, PowerShell
    DOCUMENTATION = 3,    // Browser on docs sites, PDF readers
    PRODUCTIVITY = 4,     // Word, Excel, Notion, Obsidian
    
    // Potentially productive (depends on content)
    BROWSER = 5,          // Chrome, Firefox, Edge (needs URL analysis)
    
    // Distracting categories
    SOCIAL_MEDIA = 10,    // Twitter, Facebook, Instagram
    COMMUNICATION = 11,   // Slack, Discord, Teams (debatable)
    ENTERTAINMENT = 12,   // YouTube, Netflix, Games
    SHOPPING = 13,        // Amazon, eBay, etc.
};

/**
 * Helper: Is this category productive?
 * 
 * Used to determine FOCUSED vs DISTRACTED state.
 */
inline bool is_productive_category(AppCategory cat) {
    return static_cast<uint8_t>(cat) < 10;  // Categories 0-9 are productive
}

/**
 * ContextSnapshot: Point-in-time capture of work context
 * 
 * SIZE: ~800 bytes (not cache-aligned, stored in buffer)
 * CAPTURE RATE: Every 30 seconds while in FOCUSED state
 * RETENTION: Last 20 snapshots (~10 minutes)
 * 
 * EDUCATIONAL NOTE: Fixed-Size Strings
 * =====================================
 * We use char arrays instead of std::string because:
 * 
 * 1. std::string allocates on heap:
 *    std::string title = "main.py - VS Code";
 *    // Allocates 17 bytes on heap (malloc, slow)
 *    // Copying requires deep copy (another malloc)
 * 
 * 2. char array is inline:
 *    char title[256] = "main.py - VS Code";
 *    // Data stored directly in struct (no heap)
 *    // Copying with memcpy (fast, no malloc)
 * 
 * Trade-off: We waste space if strings are short.
 * But for a 20-element buffer, this is negligible (20 * 800 = 16KB).
 */
struct ContextSnapshot {
    //==========================================================================
    // TIMING (16 bytes)
    //==========================================================================
    
    uint64_t timestamp_us;           // When this snapshot was taken
    uint32_t duration_in_context_s;  // How long user has been in this context
    uint32_t focus_streak_s;         // Total focus streak (across context switches)
    
    //==========================================================================
    // APPLICATION CONTEXT (304 bytes)
    //==========================================================================
    
    char app_name[32];               // Process name: "Code.exe"
    char window_title[256];          // Full title: "main.py - src - Project - VS Code"
    uint32_t process_id;             // Windows PID
    uint32_t window_handle;          // HWND for this window
    AppCategory category;            // Classification
    uint8_t padding1[3];             // Alignment padding
    
    //==========================================================================
    // PARSED CONTEXT (392 bytes)
    //==========================================================================
    
    /**
     * IDE-specific context extracted from window title.
     * 
     * Example: VS Code title "main.py:234 - src - MyProject - Visual Studio Code"
     * Extracted:
     *   file_path = "main.py"
     *   line_number = 234
     *   project_name = "MyProject"
     *   folder_path = "src"
     */
    char file_path[260];             // MAX_PATH on Windows
    uint32_t line_number;            // Line number if detectable (0 = unknown)
    char function_name[64];          // Current function if detectable
    char project_name[64];           // Project/workspace name
    
    //==========================================================================
    // BROWSER CONTEXT (192 bytes)
    //==========================================================================
    
    /**
     * Browser-specific context.
     * 
     * We capture domain (not full URL) for privacy.
     * Example: "github.com/secret-project/file.py" → domain = "github.com"
     * 
     * Also capture search queries for context:
     * "python async await - Google Search" → query = "python async await"
     */
    char browser_domain[64];         // "stackoverflow.com", "github.com"
    char last_search_query[128];     // Most recent search query
    
    //==========================================================================
    // ACTIVITY METRICS (24 bytes)
    //==========================================================================
    
    /**
     * Activity statistics during this context.
     * 
     * Used to determine engagement level:
     * - High keystrokes + low switches = deep focus
     * - Low keystrokes + high switches = distracted browsing
     */
    uint32_t keystrokes_in_context;  // Total keystrokes since entering context
    uint32_t mouse_clicks;           // Total clicks
    uint32_t mouse_distance_px;      // Total mouse travel (pixels)
    uint32_t context_switches;       // App switches during this snapshot period
    float typing_speed_cpm;          // Characters per minute (averaged)
    float focus_score;               // ML-predicted focus score (0-100)
    
    //==========================================================================
    // FLAGS (8 bytes)
    //==========================================================================
    
    uint8_t has_unsaved_changes;     // IDE shows unsaved indicator (•)
    uint8_t is_debugging;            // In debug mode (detected from title)
    uint8_t is_building;             // Build in progress
    uint8_t is_productive;           // Was this productive time?
    uint8_t padding2[4];             // Alignment
    
    //==========================================================================
    // HELPER METHODS
    //==========================================================================
    
    /**
     * Clear all fields (for initialization)
     */
    void clear() {
        std::memset(this, 0, sizeof(ContextSnapshot));
    }
    
    /**
     * Get a brief description of this context
     * 
     * Returns: "main.py in VS Code" or "stackoverflow.com in Chrome"
     */
    const char* brief_description() const {
        static char buffer[128];
        
        if (std::strlen(file_path) > 0) {
            // IDE context: show file
            const char* filename = std::strrchr(file_path, '\\');
            if (!filename) filename = std::strrchr(file_path, '/');
            filename = filename ? filename + 1 : file_path;
            
            if (line_number > 0) {
                std::snprintf(buffer, sizeof(buffer), "%s:%u", filename, line_number);
            } else {
                std::snprintf(buffer, sizeof(buffer), "%s", filename);
            }
        }
        else if (std::strlen(browser_domain) > 0) {
            // Browser context: show domain
            std::snprintf(buffer, sizeof(buffer), "%s", browser_domain);
        }
        else {
            // Generic: show app name
            std::snprintf(buffer, sizeof(buffer), "%s", app_name);
        }
        
        return buffer;
    }
    
    /**
     * Check if this is a meaningful context worth showing
     */
    bool is_meaningful() const {
        // Must have app name
        if (std::strlen(app_name) == 0) return false;
        
        // Must have spent some time (at least 5 seconds)
        if (duration_in_context_s < 5) return false;
        
        // Must have some activity (not just idle)
        if (keystrokes_in_context == 0 && mouse_clicks == 0) return false;
        
        return true;
    }
};

#pragma pack(pop)

// Verify reasonable size (should be around 800 bytes)
static_assert(sizeof(ContextSnapshot) < 1024, "ContextSnapshot too large");

/**
 * ContextHistory: Circular buffer of recent context snapshots
 * 
 * PATTERN: Circular Buffer (Ring Buffer)
 * =======================================
 * 
 * Why circular buffer?
 * - Fixed memory (no growth, no fragmentation)
 * - Old entries automatically evicted (no manual cleanup)
 * - O(1) push, O(1) access to recent entries
 * 
 * Structure:
 * 
 *   [0] [1] [2] [3] [4] [5] [6] [7]  ← Buffer (8 slots for this example)
 *            ↑           ↑
 *          tail        head
 *          (oldest)    (newest)
 * 
 * When we push:
 *   1. Write to buffer[head]
 *   2. Advance head = (head + 1) % capacity
 *   3. If head == tail, advance tail (evict oldest)
 * 
 * EDUCATIONAL NOTE: Why Not std::vector?
 * =======================================
 * 
 * std::vector grows dynamically:
 *   vector.push_back(x);  // Might allocate new memory!
 *   // Old pointers invalidated, memory fragmentation
 * 
 * Circular buffer is fixed:
 *   buffer.push(x);  // Always O(1), no allocation
 *   // Memory layout predictable, cache-friendly
 * 
 * Trade-off: We set capacity upfront (can't grow).
 * But for context history, 20 snapshots (10 min) is plenty.
 */
class ContextHistory {
public:
    static constexpr size_t CAPACITY = 20;  // ~10 minutes at 30s intervals
    
private:
    ContextSnapshot snapshots_[CAPACITY];
    size_t head_ = 0;      // Next write position
    size_t tail_ = 0;      // Oldest valid entry
    size_t count_ = 0;     // Number of valid entries (0 to CAPACITY)
    
public:
    //==========================================================================
    // MODIFIERS
    //==========================================================================
    
    /**
     * Push a new snapshot (evicts oldest if full)
     * 
     * Time Complexity: O(1)
     * 
     * EDUCATIONAL NOTE: Value vs Reference Parameters
     * ================================================
     * 
     * void push(ContextSnapshot snapshot);      // Pass by value (copies ~800 bytes!)
     * void push(ContextSnapshot& snapshot);     // Pass by reference (only pointer)
     * void push(const ContextSnapshot& s);      // Pass by const reference (safest)
     * 
     * We use const reference:
     * - No copy on function call (efficient)
     * - Cannot modify caller's data (safe)
     * - Compiler can optimize (no aliasing concerns)
     */
    void push(const ContextSnapshot& snapshot) {
        // Write to current head position
        snapshots_[head_] = snapshot;  // Copy into buffer
        
        // Advance head (with wraparound)
        head_ = (head_ + 1) % CAPACITY;
        
        // Update count or advance tail if full
        if (count_ < CAPACITY) {
            count_++;
        } else {
            // Buffer was full, oldest entry evicted
            tail_ = (tail_ + 1) % CAPACITY;
        }
    }
    
    /**
     * Clear all entries
     */
    void clear() {
        head_ = tail_ = count_ = 0;
    }
    
    //==========================================================================
    // ACCESSORS
    //==========================================================================
    
    /**
     * Get the most recent snapshot
     * 
     * Returns: Pointer to newest snapshot, or nullptr if empty
     */
    const ContextSnapshot* get_last() const {
        if (count_ == 0) return nullptr;
        
        // Head points to NEXT write position, so last written is head-1
        size_t last_idx = (head_ + CAPACITY - 1) % CAPACITY;
        return &snapshots_[last_idx];
    }
    
    /**
     * Get snapshot by index (0 = most recent, 1 = second most recent, etc.)
     * 
     * Returns: Pointer to snapshot, or nullptr if index out of range
     */
    const ContextSnapshot* get_at(size_t index) const {
        if (index >= count_) return nullptr;
        
        // Calculate actual array index
        // Most recent is at (head - 1), so (head - 1 - index)
        size_t actual_idx = (head_ + CAPACITY - 1 - index) % CAPACITY;
        return &snapshots_[actual_idx];
    }
    
    /**
     * Get last N snapshots (most recent first)
     * 
     * Fills output array with up to max_count snapshots.
     * Returns: Number of snapshots actually copied
     */
    size_t get_recent(ContextSnapshot* output, size_t max_count) const {
        size_t to_copy = std::min(max_count, count_);
        
        for (size_t i = 0; i < to_copy; i++) {
            output[i] = *get_at(i);
        }
        
        return to_copy;
    }
    
    /**
     * Find most recent snapshot for a specific app
     * 
     * Useful for: "What was I doing in VS Code?"
     */
    const ContextSnapshot* find_by_app(const char* app_name) const {
        for (size_t i = 0; i < count_; i++) {
            const ContextSnapshot* snap = get_at(i);
            if (snap && std::strcmp(snap->app_name, app_name) == 0) {
                return snap;
            }
        }
        return nullptr;
    }
    
    /**
     * Find most recent productive snapshot
     * 
     * Useful for: "What productive work was I doing?"
     */
    const ContextSnapshot* find_last_productive() const {
        for (size_t i = 0; i < count_; i++) {
            const ContextSnapshot* snap = get_at(i);
            if (snap && snap->is_productive && snap->is_meaningful()) {
                return snap;
            }
        }
        return nullptr;
    }
    
    //==========================================================================
    // STATISTICS
    //==========================================================================
    
    size_t size() const { return count_; }
    bool empty() const { return count_ == 0; }
    bool full() const { return count_ == CAPACITY; }
    
    /**
     * Calculate total focus time in history
     */
    uint32_t total_focus_seconds() const {
        uint32_t total = 0;
        for (size_t i = 0; i < count_; i++) {
            const ContextSnapshot* snap = get_at(i);
            if (snap && snap->is_productive) {
                total += snap->duration_in_context_s;
            }
        }
        return total;
    }
};

/**
 * DistractionState: State machine for tracking focus/distraction cycles
 * 
 * STATE MACHINE PATTERN:
 * =======================
 * 
 * Instead of scattered boolean flags:
 *   bool is_focused;
 *   bool was_distracted;
 *   bool should_show_overlay;
 *   // Which combinations are valid? Hard to track!
 * 
 * We use explicit states:
 *   enum State { FOCUSED, DISTRACTED, RECOVERING };
 *   // Clear transitions, easy to reason about
 * 
 * Transitions:
 *   FOCUSED → DISTRACTED:     User switches to distracting app
 *   DISTRACTED → RECOVERING:  User returns to productive app
 *   RECOVERING → FOCUSED:     Overlay dismissed
 */
enum class DistractionState : uint8_t {
    FOCUSED = 0,      // User is in productive app
    DISTRACTED = 1,   // User is in distracting app
    RECOVERING = 2,   // User returned, showing overlay
};

/**
 * RecoveryContext: Data prepared for the recovery overlay
 * 
 * When user returns from distraction, we compile this struct
 * with all the information needed to display the overlay.
 */
struct RecoveryContext {
    // Primary context (what they were doing before distraction)
    ContextSnapshot last_productive;
    
    // Distraction info
    uint64_t distraction_start_us;   // When they got distracted
    uint64_t distraction_end_us;     // When they returned (now)
    uint32_t distraction_duration_s; // How long they were away
    char distraction_app[32];        // What app distracted them
    
    // Recent activity (for "what was I doing" list)
    static constexpr size_t MAX_RECENT_ACTIVITIES = 5;
    struct Activity {
        char description[128];       // "Edited main.py (45 keystrokes)"
        uint64_t timestamp_us;
    };
    Activity recent_activities[MAX_RECENT_ACTIVITIES];
    size_t activity_count;
    
    // Statistics
    uint32_t focus_duration_before_s;  // How long were they focused?
    uint32_t context_switches_today;   // Total distractions today
    
    /**
     * Build recovery context from history
     * 
     * Called when user returns from distraction.
     */
    void build_from_history(const ContextHistory& history, 
                            uint64_t distraction_start,
                            uint64_t distraction_end,
                            const char* distraction_app_name) {
        // Find last productive context
        const ContextSnapshot* last = history.find_last_productive();
        if (last) {
            last_productive = *last;
        } else {
            last_productive.clear();
        }
        
        // Set distraction timing
        distraction_start_us = distraction_start;
        distraction_end_us = distraction_end;
        distraction_duration_s = static_cast<uint32_t>((distraction_end - distraction_start) / 1000000);
        
        // Copy distraction app name
        std::strncpy(distraction_app, distraction_app_name, sizeof(distraction_app) - 1);
        distraction_app[sizeof(distraction_app) - 1] = '\0';
        
        // Get recent activities (simplified - would generate descriptions in real impl)
        activity_count = 0;
        for (size_t i = 0; i < std::min(history.size(), MAX_RECENT_ACTIVITIES); i++) {
            const ContextSnapshot* snap = history.get_at(i);
            if (snap && snap->is_meaningful()) {
                std::snprintf(recent_activities[activity_count].description,
                             sizeof(recent_activities[activity_count].description),
                             "Working in %s", snap->brief_description());
                recent_activities[activity_count].timestamp_us = snap->timestamp_us;
                activity_count++;
            }
        }
        
        // Calculate focus duration before distraction
        focus_duration_before_s = history.total_focus_seconds();
    }
};

#endif // NEUROFOCUS_CONTEXT_H
