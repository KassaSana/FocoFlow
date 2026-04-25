/**
 * Neural Focus: Context Tracker
 * 
 * This is the main coordinator for context recovery. It:
 * 1. Captures context snapshots periodically
 * 2. Tracks distraction state (FOCUSED → DISTRACTED → RECOVERING)
 * 3. Detects when user returns from distraction
 * 4. Shows the recovery overlay with context
 * 
 * DESIGN PATTERN: Mediator
 * =========================
 * 
 * The ContextTracker coordinates between components:
 * - Event stream (window focus changes)
 * - Context snapshots (what user was doing)
 * - Overlay window (displaying context)
 * 
 * Instead of these components talking directly to each other,
 * they communicate through the tracker, keeping the system loosely coupled.
 * 
 * STATE MACHINE:
 * ==============
 * 
 *   ┌─────────────────────────────────────────────────────────┐
 *   │                                                         │
 *   │  ┌──────────┐  switch to      ┌─────────────┐           │
 *   │  │          │  distracting    │             │           │
 *   │  │ FOCUSED  ├─────────────────► DISTRACTED  │           │
 *   │  │          │                 │             │           │
 *   │  └────▲─────┘                 └──────┬──────┘           │
 *   │       │                              │                  │
 *   │       │ overlay                      │ return to        │
 *   │       │ dismissed                    │ productive       │
 *   │       │                              │                  │
 *   │  ┌────┴─────┐                        │                  │
 *   │  │          │◄───────────────────────┘                  │
 *   │  │RECOVERING│   if distraction > 30s, show overlay      │
 *   │  │          │                                           │
 *   │  └──────────┘                                           │
 *   │                                                         │
 *   └─────────────────────────────────────────────────────────┘
 */

#ifndef NEUROFOCUS_CONTEXT_TRACKER_H
#define NEUROFOCUS_CONTEXT_TRACKER_H

#include "context.h"
#include "title_parser.h"
#include "overlay.h"
#include "event.h"
#include <chrono>
#include <mutex>

/**
 * TrackerConfig: Tuning parameters for context tracking
 */
struct TrackerConfig {
    // Snapshot interval (how often to capture context while focused)
    uint32_t snapshot_interval_ms = 30000;  // Every 30 seconds
    
    // Minimum distraction time to trigger recovery overlay
    uint32_t min_distraction_for_recovery_ms = 30000;  // 30 seconds
    
    // Time before we consider user "idle" (no input)
    uint32_t idle_timeout_ms = 120000;  // 2 minutes
    
    // Show overlay for short distractions? (like checking Slack)
    bool show_for_short_distractions = false;
    
    // Apps that should be considered productive (custom list)
    const char* custom_productive_apps[16] = {nullptr};
    
    // Apps that should be considered distracting (custom list)
    const char* custom_distracting_apps[16] = {nullptr};
};

/**
 * ContextTracker: Main coordinator for context recovery
 * 
 * Usage:
 * 
 *   ContextTracker tracker;
 *   tracker.start();
 *   
 *   // When window focus changes:
 *   tracker.on_window_change("Code.exe", "main.py - VS Code");
 *   
 *   // When user types:
 *   tracker.on_keystroke();
 *   
 *   // Periodically (or in main loop):
 *   tracker.update();
 */
class ContextTracker {
public:
    //==========================================================================
    // LIFECYCLE
    //==========================================================================
    
    ContextTracker() {
        overlay_.create();
        overlay_.on_dismiss([this]() { on_overlay_dismissed(); });
    }
    
    ~ContextTracker() {
        overlay_.destroy();
    }
    
    /**
     * Start tracking
     * 
     * Initializes state, starts snapshot timer.
     */
    void start() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        state_ = DistractionState::FOCUSED;
        last_snapshot_time_ = now_us();
        focus_start_time_ = now_us();
        
        // Take initial snapshot
        current_context_.clear();
        current_context_.timestamp_us = now_us();
    }
    
    /**
     * Stop tracking
     * 
     * Hides overlay, clears state.
     */
    void stop() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        overlay_.hide();
        history_.clear();
    }
    
    //==========================================================================
    // EVENT HANDLERS (call these from your event loop)
    //==========================================================================
    
    /**
     * Handle window focus change
     * 
     * Called when user switches to a different window.
     * This is the main trigger for state transitions.
     */
    void on_window_change(const char* app_name, const char* window_title) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Parse the new window context
        ContextSnapshot new_context;
        parser::parse_window_title(app_name, window_title, new_context);
        new_context.timestamp_us = now_us();
        
        // Calculate time in previous context
        if (current_context_.timestamp_us > 0) {
            current_context_.duration_in_context_s = 
                (now_us() - current_context_.timestamp_us) / 1000000;
        }
        
        // Save current context to history (if meaningful)
        if (current_context_.is_meaningful()) {
            history_.push(current_context_);
        }
        
        // State machine transitions
        bool was_productive = is_productive_category(current_context_.category);
        bool now_productive = is_productive_category(new_context.category);
        
        switch (state_) {
            case DistractionState::FOCUSED:
                if (!now_productive) {
                    // FOCUSED → DISTRACTED
                    state_ = DistractionState::DISTRACTED;
                    distraction_start_time_ = now_us();
                    distraction_app_ = app_name;
                }
                break;
                
            case DistractionState::DISTRACTED:
                if (now_productive) {
                    // DISTRACTED → Check if should show overlay
                    uint64_t distraction_duration_us = now_us() - distraction_start_time_;
                    uint64_t min_duration_us = config_.min_distraction_for_recovery_ms * 1000ULL;
                    
                    if (distraction_duration_us >= min_duration_us) {
                        // Long enough distraction - show recovery overlay
                        state_ = DistractionState::RECOVERING;
                        show_recovery_overlay();
                    } else {
                        // Short distraction - go straight to focused
                        state_ = DistractionState::FOCUSED;
                        focus_start_time_ = now_us();
                    }
                }
                break;
                
            case DistractionState::RECOVERING:
                // Stay in recovering until overlay is dismissed
                break;
        }
        
        // Update current context
        current_context_ = new_context;
        
        // Update focus streak
        if (now_productive && state_ == DistractionState::FOCUSED) {
            current_context_.focus_streak_s = (now_us() - focus_start_time_) / 1000000;
        }
    }
    
    /**
     * Handle keystroke event
     * 
     * Updates activity metrics in current context.
     */
    void on_keystroke() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        current_context_.keystrokes_in_context++;
        last_activity_time_ = now_us();
    }
    
    /**
     * Handle mouse click
     */
    void on_mouse_click() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        current_context_.mouse_clicks++;
        last_activity_time_ = now_us();
    }
    
    /**
     * Handle mouse movement
     * 
     * Updates total distance traveled.
     */
    void on_mouse_move(int dx, int dy) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Approximate distance (Manhattan distance for speed)
        current_context_.mouse_distance_px += (dx > 0 ? dx : -dx) + (dy > 0 ? dy : -dy);
        last_activity_time_ = now_us();
    }
    
    //==========================================================================
    // PERIODIC UPDATE (call from main loop or timer)
    //==========================================================================
    
    /**
     * Update tracker state
     * 
     * Should be called periodically (e.g., every second).
     * Handles:
     * - Taking periodic snapshots
     * - Detecting idle state
     */
    void update() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        uint64_t now = now_us();
        
        // Check for idle
        uint64_t time_since_activity = now - last_activity_time_;
        if (time_since_activity > config_.idle_timeout_ms * 1000ULL) {
            // User is idle - this isn't really a distraction, more like AFK
            // We might want to pause tracking here
        }
        
        // Take periodic snapshot if in FOCUSED state
        if (state_ == DistractionState::FOCUSED) {
            uint64_t time_since_snapshot = now - last_snapshot_time_;
            if (time_since_snapshot >= config_.snapshot_interval_ms * 1000ULL) {
                take_snapshot();
                last_snapshot_time_ = now;
            }
        }
    }
    
    //==========================================================================
    // ACCESSORS
    //==========================================================================
    
    DistractionState get_state() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return state_;
    }
    
    const ContextSnapshot& get_current_context() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return current_context_;
    }
    
    const ContextHistory& get_history() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return history_;
    }
    
    /**
     * Get focus duration in current session
     */
    uint32_t get_focus_duration_seconds() const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ != DistractionState::FOCUSED) return 0;
        return (now_us() - focus_start_time_) / 1000000;
    }
    
    //==========================================================================
    // CONFIGURATION
    //==========================================================================
    
    void set_config(const TrackerConfig& config) {
        std::lock_guard<std::mutex> lock(mutex_);
        config_ = config;
    }
    
    /**
     * Manually trigger recovery overlay (for testing)
     */
    void force_show_overlay() {
        std::lock_guard<std::mutex> lock(mutex_);
        show_recovery_overlay();
    }
    
private:
    //==========================================================================
    // INTERNAL HELPERS
    //==========================================================================
    
    /**
     * Get current time in microseconds
     * 
     * Uses steady_clock for monotonic time (won't jump if system clock changes).
     */
    static uint64_t now_us() {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::microseconds>(
            now.time_since_epoch()
        ).count();
    }
    
    /**
     * Take a context snapshot and add to history
     */
    void take_snapshot() {
        // Update timing
        current_context_.timestamp_us = now_us();
        current_context_.duration_in_context_s = 
            (now_us() - focus_start_time_) / 1000000;
        
        // Mark as productive if in FOCUSED state
        current_context_.is_productive = (state_ == DistractionState::FOCUSED);
        
        // Add to history if meaningful
        if (current_context_.is_meaningful()) {
            history_.push(current_context_);
        }
    }
    
    /**
     * Show the recovery overlay with context
     */
    void show_recovery_overlay() {
        // Build recovery context
        RecoveryContext recovery;
        recovery.build_from_history(
            history_,
            distraction_start_time_,
            now_us(),
            distraction_app_.c_str()
        );
        
        // Calculate focus duration before distraction
        if (distraction_start_time_ > focus_start_time_) {
            recovery.focus_duration_before_s = 
                (distraction_start_time_ - focus_start_time_) / 1000000;
        }
        
        // Show overlay
        overlay_.show(recovery);
    }
    
    /**
     * Called when overlay is dismissed
     */
    void on_overlay_dismissed() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (state_ == DistractionState::RECOVERING) {
            // Transition to FOCUSED
            state_ = DistractionState::FOCUSED;
            focus_start_time_ = now_us();
            
            // Reset activity counters
            current_context_.keystrokes_in_context = 0;
            current_context_.mouse_clicks = 0;
            current_context_.mouse_distance_px = 0;
        }
    }
    
    //==========================================================================
    // STATE
    //==========================================================================
    
    mutable std::mutex mutex_;           // Thread safety
    
    TrackerConfig config_;               // Configuration
    DistractionState state_ = DistractionState::FOCUSED;
    
    // Current context
    ContextSnapshot current_context_;
    
    // History
    ContextHistory history_;
    
    // Timing
    uint64_t focus_start_time_ = 0;      // When current focus session started
    uint64_t distraction_start_time_ = 0;// When user got distracted
    uint64_t last_snapshot_time_ = 0;    // When last snapshot was taken
    uint64_t last_activity_time_ = 0;    // Last keyboard/mouse activity
    
    // Distraction tracking
    std::string distraction_app_;        // App that distracted user
    
    // Overlay
    RecoveryOverlay overlay_;
};

#endif // NEUROFOCUS_CONTEXT_TRACKER_H
