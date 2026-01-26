/**
 * Neural Focus: Event Processor Test
 * 
 * Tests the full pipeline: hooks → ring buffer → consumer
 * 
 * BUILD:
 *   g++ -std=c++17 processor_test.cpp event_processor.cpp windows_hooks.cpp \
 *       -o processor_test.exe -luser32 -lgdi32 -lpsapi
 * 
 * USAGE:
 *   processor_test.exe
 *   (Press Ctrl+C to stop)
 */

#include <cstdio>
#include <cstdlib>
#include <csignal>
#include "event_processor.h"
#include <thread>
#include <chrono>

// Global flag for shutdown
static volatile bool g_running = true;

// Signal handler
void signal_handler(int signal) {
    printf("\n[Test] Shutting down...\n");
    g_running = false;
}

// Convert event type to string
const char* event_type_str(EventType type) {
    switch (type) {
        case EventType::KEY_PRESS: return "KEY_PRESS";
        case EventType::KEY_RELEASE: return "KEY_RELEASE";
        case EventType::MOUSE_MOVE: return "MOUSE_MOVE";
        case EventType::MOUSE_CLICK: return "MOUSE_CLICK";
        case EventType::MOUSE_WHEEL: return "MOUSE_WHEEL";
        case EventType::WINDOW_FOCUS_CHANGE: return "WIN_FOCUS";
        default: return "UNKNOWN";
    }
}

int main() {
    printf("==========================================\n");
    printf("  Neural Focus: Event Processor Test\n");
    printf("==========================================\n\n");
    fflush(stdout);
    
    // Set up Ctrl+C handler
    signal(SIGINT, signal_handler);
    
    // Create processor with mouse move filtering
    neurofocus::EventProcessor processor;
    
    neurofocus::ProcessorConfig config;
    config.capture_keyboard = true;
    config.capture_mouse = true;
    config.capture_window_focus = true;
    config.filter_mouse_move = true;      // Filter mouse moves
    config.mouse_move_sample_rate = 20;   // Keep every 20th
    
    printf("Starting processor...\n");
    fflush(stdout);
    
    // Start processor
    if (!processor.start(config)) {
        fprintf(stderr, "Failed to start processor\n");
        fflush(stderr);
        return 1;
    }
    
    printf("Processor running!\n");
    printf("- Keyboard: ON\n");
    printf("- Mouse: ON (sampled 1/%d)\n", config.mouse_move_sample_rate);
    printf("- Window focus: ON\n");
    printf("- Press Ctrl+C to stop\n\n");
    
    // Consumer loop - simulates what ML service would do
    uint64_t events_consumed = 0;
    auto start_time = std::chrono::steady_clock::now();
    auto last_stats_time = start_time;
    
    while (g_running) {
        // Try to consume events
        Event event;
        bool got_event = false;
        
        while (processor.try_pop(event)) {
            got_event = true;
            events_consumed++;
            
            // Print some events (not all, to avoid spam)
            if (events_consumed % 50 == 0) {
                printf("[%6llu] %-12s app=%-15s\n",
                       events_consumed,
                       event_type_str(event.event_type),
                       event.app_name);
            }
        }
        
        // If no events, sleep briefly to avoid busy-waiting
        if (!got_event) {
#ifdef _WIN32
            Sleep(1);
#else
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
#endif
        }
        
        // Print stats every 3 seconds
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - last_stats_time).count();
        
        if (elapsed >= 3) {
            last_stats_time = now;
            
            const auto& stats = processor.stats();
            printf("\n--- Stats ---\n");
            printf("Received:    %llu\n", stats.events_received.load());
            printf("Queued:      %llu\n", stats.events_queued.load());
            printf("Consumed:    %llu\n", events_consumed);
            printf("Dropped:     %llu (%.2f%%)\n", 
                   stats.events_dropped.load(),
                   stats.drop_rate());
            printf("Buffer:      %u / %zu (high: %u)\n",
                   stats.buffer_size.load(),
                   neurofocus::EventProcessor::BUFFER_SIZE,
                   stats.buffer_high_water.load());
            printf("-------------\n\n");
        }
    }
    
    // Final stats
    auto total_time = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - start_time).count();
    
    processor.stop();
    
    const auto& stats = processor.stats();
    printf("\n==========================================\n");
    printf("  Final Results (%lld seconds)\n", total_time);
    printf("==========================================\n");
    printf("Events received:  %llu\n", stats.events_received.load());
    printf("Events queued:    %llu\n", stats.events_queued.load());
    printf("Events consumed:  %llu\n", events_consumed);
    printf("Events dropped:   %llu (%.2f%%)\n", 
           stats.events_dropped.load(),
           stats.drop_rate());
    printf("Buffer high:      %u / %zu\n",
           stats.buffer_high_water.load(),
           neurofocus::EventProcessor::BUFFER_SIZE);
    
    if (total_time > 0) {
        printf("Throughput:       %llu events/sec\n", 
               stats.events_received.load() / total_time);
    }
    
    printf("\n");
    
    // Check for issues
    if (stats.events_dropped.load() > 0) {
        printf("⚠️  WARNING: Events were dropped! Consumer too slow.\n");
    } else {
        printf("✅ SUCCESS: No events dropped!\n");
    }
    
    return 0;
}
