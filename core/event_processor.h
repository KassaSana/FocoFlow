/**
 * Neural Focus: Event Processor Interface
 * 
 * The Event Processor is the main coordinator that:
 * 1. Receives events from Windows hooks
 * 2. Pushes them to the ring buffer
 * 3. Tracks statistics (drops, latency)
 * 
 * Design Goals:
 * - Simple: One class that coordinates hooks + buffer
 * - Fast: Callback path must be <100μs
 * - Observable: Expose stats for monitoring
 */

#pragma once

#include "event.h"
#include "ring_buffer.h"
#include "windows_hooks.h"
#include <atomic>
#include <chrono>
#include <thread>

namespace neurofocus {

/**
 * Statistics tracked by the processor.
 */
struct ProcessorStats {
    // Event counts
    std::atomic<uint64_t> events_received{0};
    std::atomic<uint64_t> events_queued{0};
    std::atomic<uint64_t> events_dropped{0};
    
    // Latency tracking (in microseconds)
    std::atomic<uint64_t> total_latency_us{0};
    std::atomic<uint64_t> max_latency_us{0};
    
    // Buffer state
    std::atomic<uint32_t> buffer_size{0};
    std::atomic<uint32_t> buffer_high_water{0};
    
    // Computed stats (call calculate() first)
    double drop_rate() const {
        uint64_t recv = events_received.load();
        if (recv == 0) return 0.0;
        return static_cast<double>(events_dropped.load()) / recv * 100.0;
    }
    
    double avg_latency_us() const {
        uint64_t queued = events_queued.load();
        if (queued == 0) return 0.0;
        return static_cast<double>(total_latency_us.load()) / queued;
    }
};

/**
 * Configuration for the event processor.
 */
struct ProcessorConfig {
    bool capture_keyboard = true;
    bool capture_mouse = true;
    bool capture_window_focus = true;
    bool filter_mouse_move = false;  // Optionally filter high-frequency moves
    uint32_t mouse_move_sample_rate = 10;  // Keep every Nth mouse move
};

/**
 * EventProcessor: Main coordinator for event capture.
 * 
 * Usage:
 *   EventProcessor processor;
 *   processor.start(config);
 *   
 *   // Events flow: hooks → ring buffer
 *   // Consumer can read from buffer on another thread
 *   
 *   while (running) {
 *       Event event;
 *       if (processor.try_pop(event)) {
 *           // Process event
 *       }
 *   }
 *   
 *   processor.stop();
 */
class EventProcessor {
public:
    // Buffer size: 65536 events = ~4MB memory
    static constexpr size_t BUFFER_SIZE = 65536;
    
    EventProcessor();
    ~EventProcessor();
    
    // Prevent copying
    EventProcessor(const EventProcessor&) = delete;
    EventProcessor& operator=(const EventProcessor&) = delete;
    
    /**
     * Start event capture.
     * @return true on success
     */
    bool start(const ProcessorConfig& config = ProcessorConfig{});
    
    /**
     * Stop event capture and clean up.
     */
    void stop();
    
    /**
     * Check if processor is running.
     */
    bool is_running() const { return running_; }
    
    /**
     * Try to pop an event from the buffer (non-blocking).
     * @return true if event was available
     */
    bool try_pop(Event& event);
    
    /**
     * Get current buffer size (approximate).
     */
    size_t buffer_size() const;
    
    /**
     * Get processor statistics.
     */
    const ProcessorStats& stats() const { return stats_; }
    
    /**
     * Reset statistics counters.
     */
    void reset_stats();

private:
    bool running_;
    ProcessorConfig config_;
    ProcessorStats stats_;
    
    // Ring buffer for events
    LockFreeRingBuffer<Event, BUFFER_SIZE> buffer_;
    
    // Hooks manager
    hooks::HooksManager hooks_;
    
    // Mouse move filtering
    std::atomic<uint32_t> mouse_move_counter_{0};
    
    // Event callback (called from hooks)
    void on_event(const Event& event);
    
    // Static callback wrapper (for C-style hooks API)
    static void event_callback(const Event& event);
    static EventProcessor* instance_;
};

} // namespace neurofocus
