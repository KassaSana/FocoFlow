/**
 * Neural Focus: Windows Hooks Implementation
 * 
 * This captures system-level events using Win32 low-level hooks.
 * The hooks run on a dedicated message thread to avoid blocking.
 */

#include "windows_hooks.h"
#include <cstdio>
#include <cstring>
#include <chrono>

#ifdef _WIN32
#include <Psapi.h>
#pragma comment(lib, "Psapi.lib")
#endif

namespace hooks {

// Singleton instance for static callbacks
HooksManager* HooksManager::instance_ = nullptr;

HooksManager::HooksManager()
    : running_(false)
    , stats_{}
#ifdef _WIN32
    , keyboard_hook_(nullptr)
    , mouse_hook_(nullptr)
    , winevent_hook_(nullptr)
    , message_thread_(nullptr)
    , message_thread_id_(0)
#endif
{
    instance_ = this;
}

HooksManager::~HooksManager() {
    stop();
    instance_ = nullptr;
}

bool HooksManager::start(const HooksConfig& config) {
    if (running_) {
        return true;  // Already running
    }
    
    if (!config.callback) {
        fprintf(stderr, "[HooksManager] Error: callback is required\n");
        return false;
    }
    
    config_ = config;
    stats_ = {};
    
#ifdef _WIN32
    // Create message thread for hooks
    message_thread_ = CreateThread(
        nullptr,               // Default security
        0,                     // Default stack size
        message_thread_proc,   // Thread function
        this,                  // Parameter
        0,                     // Run immediately
        &message_thread_id_    // Thread ID output
    );
    
    if (!message_thread_) {
        fprintf(stderr, "[HooksManager] Failed to create message thread: %lu\n", 
                GetLastError());
        return false;
    }
    
    // Wait for hooks to be installed (up to 1 second)
    for (int i = 0; i < 10 && !running_; ++i) {
        Sleep(100);
    }
    
    if (!running_) {
        fprintf(stderr, "[HooksManager] Hooks failed to start\n");
        stop();
        return false;
    }
    
    printf("[HooksManager] Started successfully\n");
    return true;
    
#else
    fprintf(stderr, "[HooksManager] Not implemented on this platform\n");
    return false;
#endif
}

void HooksManager::stop() {
    if (!running_) {
        return;
    }
    
#ifdef _WIN32
    printf("[HooksManager] Stopping...\n");
    
    // Unhook everything
    if (keyboard_hook_) {
        UnhookWindowsHookEx(keyboard_hook_);
        keyboard_hook_ = nullptr;
    }
    
    if (mouse_hook_) {
        UnhookWindowsHookEx(mouse_hook_);
        mouse_hook_ = nullptr;
    }
    
    if (winevent_hook_) {
        UnhookWinEvent(winevent_hook_);
        winevent_hook_ = nullptr;
    }
    
    // Stop message thread
    if (message_thread_) {
        PostThreadMessage(message_thread_id_, WM_QUIT, 0, 0);
        WaitForSingleObject(message_thread_, 1000);
        CloseHandle(message_thread_);
        message_thread_ = nullptr;
    }
    
    running_ = false;
    printf("[HooksManager] Stopped\n");
#endif
}

HooksManager::Stats HooksManager::get_stats() const {
    return stats_;
}

#ifdef _WIN32

//=============================================================================
// MESSAGE THREAD
//=============================================================================

DWORD WINAPI HooksManager::message_thread_proc(LPVOID param) {
    auto* mgr = static_cast<HooksManager*>(param);
    
    printf("[HooksManager] Message thread started\n");
    
    // Install keyboard hook
    if (mgr->config_.capture_keyboard) {
        mgr->keyboard_hook_ = SetWindowsHookEx(
            WH_KEYBOARD_LL,
            keyboard_proc,
            GetModuleHandle(nullptr),
            0  // All threads
        );
        
        if (mgr->keyboard_hook_) {
            printf("[HooksManager] Keyboard hook installed\n");
        } else {
            fprintf(stderr, "[HooksManager] Failed to install keyboard hook: %lu\n",
                    GetLastError());
        }
    }
    
    // Install mouse hook
    if (mgr->config_.capture_mouse) {
        mgr->mouse_hook_ = SetWindowsHookEx(
            WH_MOUSE_LL,
            mouse_proc,
            GetModuleHandle(nullptr),
            0  // All threads
        );
        
        if (mgr->mouse_hook_) {
            printf("[HooksManager] Mouse hook installed\n");
        } else {
            fprintf(stderr, "[HooksManager] Failed to install mouse hook: %lu\n",
                    GetLastError());
        }
    }
    
    // Install window event hook
    if (mgr->config_.capture_window_focus) {
        mgr->winevent_hook_ = SetWinEventHook(
            EVENT_SYSTEM_FOREGROUND,      // Only foreground changes
            EVENT_SYSTEM_FOREGROUND,
            nullptr,                       // No DLL
            winevent_proc,                 // Callback
            0,                             // All processes
            0,                             // All threads
            WINEVENT_OUTOFCONTEXT         // Async callback
        );
        
        if (mgr->winevent_hook_) {
            printf("[HooksManager] Window event hook installed\n");
        } else {
            fprintf(stderr, "[HooksManager] Failed to install window event hook: %lu\n",
                    GetLastError());
        }
    }
    
    mgr->running_ = true;
    
    // Message loop (required for hooks)
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    printf("[HooksManager] Message thread exiting\n");
    return 0;
}

//=============================================================================
// KEYBOARD HOOK
//=============================================================================

LRESULT CALLBACK HooksManager::keyboard_proc(int code, WPARAM wparam, LPARAM lparam) {
    if (code == HC_ACTION && instance_ && instance_->running_) {
        auto* kb = reinterpret_cast<KBDLLHOOKSTRUCT*>(lparam);
        
        // Create event
        Event event;
        event.timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count();
        
        // Determine event type
        if (wparam == WM_KEYDOWN || wparam == WM_SYSKEYDOWN) {
            event.event_type = EventType::KEY_PRESS;
        } else if (wparam == WM_KEYUP || wparam == WM_SYSKEYUP) {
            event.event_type = EventType::KEY_RELEASE;
        } else {
            return CallNextHookEx(nullptr, code, wparam, lparam);
        }
        
        // Fill keyboard data
        event.data.key.virtual_key_code = static_cast<uint32_t>(kb->vkCode);
        event.data.key.scan_code = static_cast<uint32_t>(kb->scanCode);
        event.data.key.flags = kb->flags;
        
        // Get current window info
        char title[256];
        get_window_info(
            event.app_name,
            sizeof(event.app_name),
            title,
            sizeof(title)
        );
        event.window_handle = reinterpret_cast<uint32_t>(GetForegroundWindow());
        event.process_id = 0;  // Will be filled by GetWindowThreadProcessId
        
        // Invoke callback
        if (event.is_valid()) {
            instance_->config_.callback(event);
            instance_->stats_.keyboard_events++;
            instance_->stats_.total_events++;
        }
    }
    
    return CallNextHookEx(nullptr, code, wparam, lparam);
}

//=============================================================================
// MOUSE HOOK
//=============================================================================

LRESULT CALLBACK HooksManager::mouse_proc(int code, WPARAM wparam, LPARAM lparam) {
    if (code == HC_ACTION && instance_ && instance_->running_) {
        auto* ms = reinterpret_cast<MSLLHOOKSTRUCT*>(lparam);
        
        // Create event
        Event event;
        event.timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count();
        
        // Determine event type
        switch (wparam) {
            case WM_LBUTTONDOWN:
            case WM_RBUTTONDOWN:
            case WM_MBUTTONDOWN:
                event.event_type = EventType::MOUSE_CLICK;
                event.data.mouse_click.button = (wparam == WM_LBUTTONDOWN) ? 1 :
                                         (wparam == WM_RBUTTONDOWN) ? 2 : 3;
                event.data.mouse_click.x = static_cast<int32_t>(ms->pt.x);
                event.data.mouse_click.y = static_cast<int32_t>(ms->pt.y);
                break;
                
            case WM_MOUSEMOVE:
                event.event_type = EventType::MOUSE_MOVE;
                event.data.mouse_move.x = static_cast<int32_t>(ms->pt.x);
                event.data.mouse_move.y = static_cast<int32_t>(ms->pt.y);
                event.data.mouse_move.speed_pps = 0;  // Calculate later if needed
                break;
                
            case WM_MOUSEWHEEL:
                event.event_type = EventType::MOUSE_WHEEL;
                event.data.mouse_wheel.delta = GET_WHEEL_DELTA_WPARAM(ms->mouseData);
                event.data.mouse_wheel.orientation = 0;  // Vertical
                event.data.mouse_wheel.reserved = 0;
                break;
                
            default:
                return CallNextHookEx(nullptr, code, wparam, lparam);
        }
        
        // Get current window info
        char title[256];
        get_window_info(
            event.app_name,
            sizeof(event.app_name),
            title,
            sizeof(title)
        );
        event.window_handle = reinterpret_cast<uint32_t>(GetForegroundWindow());
        event.process_id = 0;
        
        // Invoke callback
        if (event.is_valid()) {
            instance_->config_.callback(event);
            instance_->stats_.mouse_events++;
            instance_->stats_.total_events++;
        }
    }
    
    return CallNextHookEx(nullptr, code, wparam, lparam);
}

//=============================================================================
// WINDOW EVENT HOOK
//=============================================================================

void CALLBACK HooksManager::winevent_proc(
    HWINEVENTHOOK hook, DWORD event_type, HWND hwnd,
    LONG id_object, LONG id_child,
    DWORD event_thread, DWORD event_time)
{
    if (event_type == EVENT_SYSTEM_FOREGROUND && instance_ && instance_->running_) {
        // Create event
        Event event;
        event.timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count();
        event.event_type = EventType::WINDOW_FOCUS_CHANGE;
        
        // Get window info
        char title[256];
        get_window_info(
            event.app_name,
            sizeof(event.app_name),
            title,
            sizeof(title)
        );
        event.window_handle = reinterpret_cast<uint32_t>(hwnd);
        event.process_id = 0;
        event.data.window_switch.old_window = 0;  // Don't track
        event.data.window_switch.new_window = event.window_handle;
        event.data.window_switch.category_hint = 0;
        
        // Invoke callback
        if (event.is_valid()) {
            instance_->config_.callback(event);
            instance_->stats_.window_focus_events++;
            instance_->stats_.total_events++;
        }
    }
}

//=============================================================================
// HELPER FUNCTIONS
//=============================================================================

void HooksManager::get_window_info(char* app_name, size_t app_size,
                                   char* window_title, size_t title_size)
{
    // Get foreground window
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) {
        app_name[0] = '\0';
        window_title[0] = '\0';
        return;
    }
    
    // Get window title
    GetWindowTextA(hwnd, window_title, static_cast<int>(title_size));
    
    // Get process name
    DWORD process_id;
    GetWindowThreadProcessId(hwnd, &process_id);
    
    HANDLE process = OpenProcess(
        PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
        FALSE,
        process_id
    );
    
    if (process) {
        char full_path[MAX_PATH];
        if (GetModuleFileNameExA(process, nullptr, full_path, MAX_PATH)) {
            // Extract just the filename
            char* filename = strrchr(full_path, '\\');
            if (filename) {
                strncpy(app_name, filename + 1, app_size - 1);
                app_name[app_size - 1] = '\0';
            } else {
                strncpy(app_name, full_path, app_size - 1);
                app_name[app_size - 1] = '\0';
            }
        } else {
            app_name[0] = '\0';
        }
        CloseHandle(process);
    } else {
        app_name[0] = '\0';
    }
}

#endif // _WIN32

} // namespace hooks
