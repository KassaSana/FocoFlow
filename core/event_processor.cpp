/**
 * Neural Focus: Event Processor Implementation
 * 
 * Coordinates hooks → ring buffer event flow.
 */

#include "event_processor.h"
#include <cstdio>

namespace neurofocus {

// Singleton for static callback
EventProcessor* EventProcessor::instance_ = nullptr;

EventProcessor::EventProcessor()
    : running_(false)
{
    instance_ = this;
}

EventProcessor::~EventProcessor() {
    stop();
    instance_ = nullptr;
}

bool EventProcessor::start(const ProcessorConfig& config) {
    if (running_) {
        return true;  // Already running
    }
    
    config_ = config;
    reset_stats();
    
    // Configure hooks
    hooks::HooksConfig hooks_config;
    hooks_config.callback = event_callback;
    hooks_config.capture_keyboard = config.capture_keyboard;
    hooks_config.capture_mouse = config.capture_mouse;
    hooks_config.capture_window_focus = config.capture_window_focus;
    
    // Start hooks
    if (!hooks_.start(hooks_config)) {
        fprintf(stderr, "[EventProcessor] Failed to start hooks\n");
        return false;
    }
    
    running_ = true;
    printf("[EventProcessor] Started (buffer capacity: %zu)\n", BUFFER_SIZE);
    return true;
}

void EventProcessor::stop() {
    if (!running_) {
        return;
    }
    
    running_ = false;
    hooks_.stop();
    printf("[EventProcessor] Stopped\n");
}

bool EventProcessor::try_pop(Event& event) {
    return buffer_.try_pop(event);
}

size_t EventProcessor::buffer_size() const {
    return buffer_.size();
}

void EventProcessor::reset_stats() {
    stats_.events_received.store(0);
    stats_.events_queued.store(0);
    stats_.events_dropped.store(0);
    stats_.total_latency_us.store(0);
    stats_.max_latency_us.store(0);
    stats_.buffer_size.store(0);
    stats_.buffer_high_water.store(0);
    mouse_move_counter_.store(0);
}

void EventProcessor::on_event(const Event& event) {
    // Count received
    stats_.events_received.fetch_add(1, std::memory_order_relaxed);
    
    // Optional: Filter mouse moves (they're very high frequency)
    if (config_.filter_mouse_move && 
        event.event_type == EventType::MOUSE_MOVE) {
        uint32_t count = mouse_move_counter_.fetch_add(1, std::memory_order_relaxed);
        if (count % config_.mouse_move_sample_rate != 0) {
            return;  // Skip this mouse move
        }
    }
    
    // Try to push to buffer
    if (buffer_.try_push(event)) {
        stats_.events_queued.fetch_add(1, std::memory_order_relaxed);
        
        // Track buffer size
        size_t current_size = buffer_.size();
        stats_.buffer_size.store(static_cast<uint32_t>(current_size), 
                                 std::memory_order_relaxed);
        
        // Track high water mark
        uint32_t high_water = stats_.buffer_high_water.load(std::memory_order_relaxed);
        if (current_size > high_water) {
            stats_.buffer_high_water.store(static_cast<uint32_t>(current_size),
                                           std::memory_order_relaxed);
        }
    } else {
        // Buffer full - drop event
        stats_.events_dropped.fetch_add(1, std::memory_order_relaxed);
    }
}

// Static callback wrapper
void EventProcessor::event_callback(const Event& event) {
    if (instance_ && instance_->running_) {
        instance_->on_event(event);
    }
}

} // namespace neurofocus
