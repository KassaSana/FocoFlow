/**
 * Neural Focus: Windows Hooks Test
 * 
 * Simple test program to verify the hooks capture events correctly.
 * 
 * BUILD:
 *   cl /EHsc /std:c++17 hooks_test.cpp windows_hooks.cpp /link user32.lib gdi32.lib Psapi.lib
 * 
 * Or with g++:
 *   g++ -std=c++17 hooks_test.cpp windows_hooks.cpp -o hooks_test.exe -luser32 -lgdi32 -lpsapi
 * 
 * USAGE:
 *   hooks_test.exe
 *   (Press Ctrl+C to stop)
 */

#include <cstdio>
#include <cstdlib>
#include <csignal>
#include "windows_hooks.h"

// Global flag for shutdown
static volatile bool g_running = true;

// Signal handler for Ctrl+C
void signal_handler(int signal) {
    printf("\n[Test] Caught signal %d, shutting down...\n", signal);
    g_running = false;
}

// Callback for captured events
void event_callback(const Event& event) {
    static uint64_t event_count = 0;
    event_count++;
    
    // Print every 10th event to avoid spam
    if (event_count % 10 == 0) {
        printf("[%7llu] ", event_count);
        
        switch (event.event_type) {
            case EventType::KEY_PRESS:
                printf("KEY_PRESS   vk=%3u app=%-20s\n",
                       event.data.key.virtual_key_code,
                       event.app_name);
                break;
                
            case EventType::MOUSE_CLICK:
                printf("MOUSE_CLICK btn=%u pos=(%4d,%4d) app=%-20s\n",
                       event.data.mouse_click.button,
                       event.data.mouse_click.x,
                       event.data.mouse_click.y,
                       event.app_name);
                break;
                
            case EventType::MOUSE_MOVE:
                printf("MOUSE_MOVE  pos=(%4d,%4d)\n",
                       event.data.mouse_move.x,
                       event.data.mouse_move.y);
                break;
                
            case EventType::WINDOW_FOCUS_CHANGE:
                printf("WIN_FOCUS   app=%-20s hwnd=%u\n",
                       event.app_name,
                       event.window_handle);
                break;
                
            default:
                printf("OTHER       type=%d\n", static_cast<int>(event.event_type));
                break;
        }
    }
}

int main() {
    printf("===================================\n");
    printf("  Neural Focus: Hooks Test\n");
    printf("===================================\n\n");
    
    // Set up Ctrl+C handler
    signal(SIGINT, signal_handler);
    
    // Configure hooks
    hooks::HooksConfig config;
    config.callback = event_callback;
    config.capture_keyboard = true;
    config.capture_mouse = true;
    config.capture_window_focus = true;
    config.idle_threshold_seconds = 30;
    
    // Start hooks
    hooks::HooksManager mgr;
    if (!mgr.start(config)) {
        fprintf(stderr, "Failed to start hooks\n");
        return 1;
    }
    
    printf("Hooks are active!\n");
    printf("- Type on keyboard\n");
    printf("- Click mouse\n");
    printf("- Switch windows\n");
    printf("- Press Ctrl+C to stop\n\n");
    
    // Run until Ctrl+C
    while (g_running) {
#ifdef _WIN32
        Sleep(1000);
#else
        sleep(1);
#endif
        
        // Print stats every 5 seconds
        static int seconds = 0;
        if (++seconds % 5 == 0) {
            auto stats = mgr.get_stats();
            printf("\n[Stats] Total: %llu  Keyboard: %llu  Mouse: %llu  Window: %llu\n\n",
                   stats.total_events,
                   stats.keyboard_events,
                   stats.mouse_events,
                   stats.window_focus_events);
        }
    }
    
    // Clean shutdown
    printf("\n[Test] Stopping hooks...\n");
    mgr.stop();
    
    // Final stats
    auto stats = mgr.get_stats();
    printf("\n===================================\n");
    printf("  Final Statistics\n");
    printf("===================================\n");
    printf("Keyboard events:     %llu\n", stats.keyboard_events);
    printf("Mouse events:        %llu\n", stats.mouse_events);
    printf("Window focus events: %llu\n", stats.window_focus_events);
    printf("Total events:        %llu\n", stats.total_events);
    printf("\n");
    
    return 0;
}
