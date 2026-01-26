/**
 * Neural Focus: Mmap Logger Test
 *
 * BUILD:
 *   g++ -std=c++17 mmap_logger_test.cpp mmap_logger.cpp -o mmap_logger_test.exe
 */

#include "mmap_logger.h"
#include <chrono>
#include <cstdio>
#include <cstring>

using neurofocus::MmapLogger;
using neurofocus::MmapLoggerConfig;

static uint64_t now_us() {
    auto now = std::chrono::system_clock::now();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(
        now.time_since_epoch()).count();
    return static_cast<uint64_t>(us);
}

static Event make_event(uint32_t pid, uint32_t index) {
    Event event{};
    event.timestamp_us = now_us();
    event.event_type = EventType::KEY_PRESS;
    event.process_id = pid;
    std::snprintf(event.app_name, sizeof(event.app_name), "test_app.exe");
    event.window_handle = 0x1234;
    event.data.key.virtual_key_code = 65 + (index % 26);
    event.data.key.scan_code = 0;
    event.data.key.flags = 0;
    event.reserved = 0;
    return event;
}

int main() {
    MmapLogger logger;
    MmapLoggerConfig config;
    config.directory = ".";
    config.base_name = "events_test";
    config.max_file_bytes = 2ull * 1024ull * 1024ull;
    config.rotate_daily = false;

    if (!logger.open(config)) {
        std::fprintf(stderr, "Failed to open mmap logger\n");
        return 1;
    }

    const uint32_t pid = 4242;
    const uint32_t count = 1000;
    for (uint32_t i = 0; i < count; ++i) {
        Event event = make_event(pid, i);
        if (!logger.append(event)) {
            std::fprintf(stderr, "Append failed at %u\n", i);
            return 1;
        }
    }

    logger.flush();
    std::printf("Wrote %llu events to %s\n",
                static_cast<unsigned long long>(logger.events_written()),
                logger.current_path().c_str());

    logger.close();

    MmapLogger reopen;
    if (!reopen.open(config)) {
        std::fprintf(stderr, "Failed to reopen mmap logger\n");
        return 1;
    }

    std::printf("Reopen event count: %llu\n",
                static_cast<unsigned long long>(reopen.events_written()));

    return 0;
}
