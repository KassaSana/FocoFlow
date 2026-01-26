/**
 * Neural Focus: Windows Hooks Interface
 * 
 * Captures system-level events using Win32 hooks:
 * - Keyboard events (WH_KEYBOARD_LL)
 * - Mouse events (WH_MOUSE_LL)
 * - Window focus changes (SetWinEventHook)
 * - Idle detection (GetLastInputInfo)
 * 
 * Design:
 * - Simple callback-based API
 * - Non-blocking (callbacks must be fast)
 * - Thread-safe hook installation
 */

#pragma once

#ifdef _WIN32
#include <Windows.h>
#endif

#include "event.h"

namespace hooks {

/**
 * Callback function signature for captured events.
 * 
 * This function is called from the Windows hook thread.
 * MUST be fast (<100μs) to avoid blocking system events.
 * 
 * Parameters:
 *   event - The captured event
 * 
 * Note: The callback should push the event to a queue
 *       for processing on another thread.
 */
using EventCallback = void (*)(const Event& event);

/**
 * Configuration for the hooks system.
 */
struct HooksConfig {
    EventCallback callback;           // Function to call for each event
    bool capture_keyboard;            // Capture keyboard events?
    bool capture_mouse;               // Capture mouse events?
    bool capture_window_focus;        // Capture window focus changes?
    uint32_t idle_threshold_seconds;  // Seconds of inactivity = idle
    
    // Default: capture everything, 30-second idle threshold
    HooksConfig()
        : callback(nullptr)
        , capture_keyboard(true)
        , capture_mouse(true)
        , capture_window_focus(true)
        , idle_threshold_seconds(30)
    {}
};

/**
 * Main hooks manager.
 * 
 * Lifecycle:
 *   HooksManager mgr;
 *   mgr.start(config);
 *   // ... hooks are active ...
 *   mgr.stop();
 */
class HooksManager {
public:
    HooksManager();
    ~HooksManager();
    
    // Prevent copying
    HooksManager(const HooksManager&) = delete;
    HooksManager& operator=(const HooksManager&) = delete;
    
    /**
     * Start capturing events.
     * 
     * Returns:
     *   true if hooks installed successfully
     *   false on error (check GetLastError)
     */
    bool start(const HooksConfig& config);
    
    /**
     * Stop capturing events and clean up.
     * Safe to call multiple times.
     */
    void stop();
    
    /**
     * Check if hooks are currently active.
     */
    bool is_running() const { return running_; }
    
    /**
     * Get event capture statistics.
     */
    struct Stats {
        uint64_t keyboard_events;
        uint64_t mouse_events;
        uint64_t window_focus_events;
        uint64_t idle_events;
        uint64_t total_events;
    };
    
    Stats get_stats() const;

private:
    bool running_;
    HooksConfig config_;
    Stats stats_;
    
#ifdef _WIN32
    HHOOK keyboard_hook_;
    HHOOK mouse_hook_;
    HWINEVENTHOOK winevent_hook_;
    HANDLE message_thread_;
    DWORD message_thread_id_;
    
    // Static callback functions (Windows API requirement)
    static LRESULT CALLBACK keyboard_proc(int code, WPARAM wparam, LPARAM lparam);
    static LRESULT CALLBACK mouse_proc(int code, WPARAM wparam, LPARAM lparam);
    static void CALLBACK winevent_proc(
        HWINEVENTHOOK hook, DWORD event, HWND hwnd,
        LONG id_object, LONG id_child,
        DWORD event_thread, DWORD event_time);
    
    // Message loop thread
    static DWORD WINAPI message_thread_proc(LPVOID param);
    
    // Helper to get current foreground window info
    static void get_window_info(char* app_name, size_t app_size,
                               char* window_title, size_t title_size);
#endif
    
    // Singleton instance for static callbacks
    static HooksManager* instance_;
};

} // namespace hooks
