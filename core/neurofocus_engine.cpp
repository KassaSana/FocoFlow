/**
 * Neural Focus: Live Event Engine
 *
 * Captures OS events, logs to mmap, and optionally publishes over ZeroMQ.
 */

#include "event_processor.h"
#include "mmap_logger.h"
#ifdef NEUROFOCUS_HAVE_ZMQ
#include "zmq_publisher.h"
#endif

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

namespace {

#ifdef _WIN32
constexpr const char* kDefaultZmqEndpoint = "tcp://127.0.0.1:5560";
#else
constexpr const char* kDefaultZmqEndpoint = "ipc:///tmp/neurofocus_events";
#endif

struct EngineConfig {
    std::string log_dir = ".";
    std::string log_base = "events";
    bool enable_log = true;
    bool enable_zmq = true;
    std::string zmq_endpoint = kDefaultZmqEndpoint;
    bool zmq_bind = true;
    bool filter_mouse_move = true;
    uint32_t mouse_sample_rate = 20;
    int stats_interval_seconds = 3;
};

std::atomic<bool> g_running{true};

void signal_handler(int) {
    g_running.store(false);
}

void print_usage() {
    std::printf(
        "Usage: neurofocus_engine.exe [options]\n"
        "\n"
        "Options:\n"
        "  --log-dir <path>        Directory for mmap logs (default: .)\n"
        "  --log-base <name>       Log base name (default: events)\n"
        "  --no-log                Disable mmap logging\n"
        "  --zmq-endpoint <addr>   ZeroMQ endpoint (default: %s)\n"
        "  --zmq-connect           Connect instead of bind\n"
        "  --no-zmq                Disable ZeroMQ publishing\n"
        "  --mouse-sample <N>      Keep every Nth mouse move (default: 20)\n"
        "  --no-mouse-filter       Disable mouse move sampling\n"
        "  --stats-interval <sec>  Stats print interval (default: 3)\n"
        "  --help                  Show this help\n"
        ,
        kDefaultZmqEndpoint
    );
}

bool parse_int(const char* value, int* out) {
    if (!value || !out) return false;
    char* end = nullptr;
    long parsed = std::strtol(value, &end, 10);
    if (!end || *end != '\0') return false;
    *out = static_cast<int>(parsed);
    return true;
}

bool parse_uint(const char* value, uint32_t* out) {
    if (!value || !out) return false;
    char* end = nullptr;
    unsigned long parsed = std::strtoul(value, &end, 10);
    if (!end || *end != '\0') return false;
    *out = static_cast<uint32_t>(parsed);
    return true;
}

bool parse_args(int argc, char** argv, EngineConfig* config) {
    if (!config) return false;
    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (std::strcmp(arg, "--help") == 0) {
            print_usage();
            return false;
        }
        if (std::strcmp(arg, "--log-dir") == 0 && i + 1 < argc) {
            config->log_dir = argv[++i];
            continue;
        }
        if (std::strcmp(arg, "--log-base") == 0 && i + 1 < argc) {
            config->log_base = argv[++i];
            continue;
        }
        if (std::strcmp(arg, "--no-log") == 0) {
            config->enable_log = false;
            continue;
        }
        if (std::strcmp(arg, "--zmq-endpoint") == 0 && i + 1 < argc) {
            config->zmq_endpoint = argv[++i];
            continue;
        }
        if (std::strcmp(arg, "--zmq-connect") == 0) {
            config->zmq_bind = false;
            continue;
        }
        if (std::strcmp(arg, "--no-zmq") == 0) {
            config->enable_zmq = false;
            continue;
        }
        if (std::strcmp(arg, "--mouse-sample") == 0 && i + 1 < argc) {
            uint32_t sample = 0;
            if (parse_uint(argv[++i], &sample) && sample > 0) {
                config->mouse_sample_rate = sample;
            }
            continue;
        }
        if (std::strcmp(arg, "--no-mouse-filter") == 0) {
            config->filter_mouse_move = false;
            continue;
        }
        if (std::strcmp(arg, "--stats-interval") == 0 && i + 1 < argc) {
            int interval = 0;
            if (parse_int(argv[++i], &interval) && interval > 0) {
                config->stats_interval_seconds = interval;
            }
            continue;
        }

        std::fprintf(stderr, "Unknown option: %s\n", arg);
        print_usage();
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    EngineConfig config;
    if (!parse_args(argc, argv, &config)) {
        return 1;
    }

    std::signal(SIGINT, signal_handler);

    neurofocus::EventProcessor processor;
    neurofocus::ProcessorConfig processor_config;
    processor_config.capture_keyboard = true;
    processor_config.capture_mouse = true;
    processor_config.capture_window_focus = true;
    processor_config.filter_mouse_move = config.filter_mouse_move;
    processor_config.mouse_move_sample_rate = config.mouse_sample_rate;

    neurofocus::MmapLogger logger;
    bool log_ready = false;
    if (config.enable_log) {
        neurofocus::MmapLoggerConfig log_config;
        log_config.directory = config.log_dir;
        log_config.base_name = config.log_base;
        if (!logger.open(log_config)) {
            std::fprintf(stderr, "[Engine] Failed to open log directory %s\n",
                         config.log_dir.c_str());
        } else {
            log_ready = true;
            std::printf("[Engine] Logging to %s\n", logger.current_path().c_str());
        }
    }

#ifdef NEUROFOCUS_HAVE_ZMQ
    neurofocus::ZmqPublisher publisher;
    bool zmq_ready = false;
    if (config.enable_zmq) {
        neurofocus::ZmqPublisherConfig zmq_config;
        zmq_config.endpoint = config.zmq_endpoint;
        zmq_config.bind = config.zmq_bind;
        if (!publisher.start(zmq_config)) {
            std::fprintf(stderr, "[Engine] ZeroMQ publisher failed\n");
        } else {
            zmq_ready = true;
        }
    }
#else
    bool zmq_ready = false;
    if (config.enable_zmq) {
        std::fprintf(stderr, "[Engine] ZeroMQ not available in this build\n");
    }
#endif

    if (!processor.start(processor_config)) {
        std::fprintf(stderr, "[Engine] Failed to start event processor\n");
        return 1;
    }

    std::printf("[Engine] Running. Press Ctrl+C to stop.\n");

    uint64_t log_failures = 0;
    uint64_t publish_failures = 0;
    auto last_stats = std::chrono::steady_clock::now();

    while (g_running.load()) {
        neurofocus::Event event;
        bool got_event = false;

        while (processor.try_pop(event)) {
            got_event = true;
            if (log_ready && !logger.append(event)) {
                log_failures++;
            }
#ifdef NEUROFOCUS_HAVE_ZMQ
            if (zmq_ready && !publisher.publish(event)) {
                publish_failures++;
            }
#endif
        }

        if (!got_event) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_stats);
        if (elapsed.count() >= config.stats_interval_seconds) {
            last_stats = now;
            const auto& stats = processor.stats();
            std::printf(
                "\n--- Engine Stats ---\n"
                "Received:    %llu\n"
                "Queued:      %llu\n"
                "Dropped:     %llu (%.2f%%)\n"
                "Buffer:      %u / %zu\n"
                "Log events:  %llu (fails: %llu)\n"
                "ZMQ fails:   %llu\n"
                "--------------------\n\n",
                static_cast<unsigned long long>(stats.events_received.load()),
                static_cast<unsigned long long>(stats.events_queued.load()),
                static_cast<unsigned long long>(stats.events_dropped.load()),
                stats.drop_rate(),
                stats.buffer_size.load(),
                neurofocus::EventProcessor::BUFFER_SIZE,
                static_cast<unsigned long long>(logger.events_written()),
                static_cast<unsigned long long>(log_failures),
                static_cast<unsigned long long>(publish_failures));
        }
    }

    processor.stop();
#ifdef NEUROFOCUS_HAVE_ZMQ
    if (zmq_ready) {
        publisher.stop();
    }
#endif
    if (log_ready) {
        logger.flush();
        logger.close();
    }

    std::printf("[Engine] Shutdown complete\n");
    return 0;
}
