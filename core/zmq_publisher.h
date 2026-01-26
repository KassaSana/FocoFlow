/**
 * Neural Focus: ZeroMQ Publisher
 *
 * Publishes raw Event bytes over ZeroMQ (PUB socket).
 */

#pragma once

#include "event.h"
#include <cstdint>
#include <string>

namespace neurofocus {

#ifdef _WIN32
constexpr const char* kDefaultZmqEndpoint = "tcp://127.0.0.1:5560";
#else
constexpr const char* kDefaultZmqEndpoint = "ipc:///tmp/neurofocus_events";
#endif

struct ZmqPublisherConfig {
    std::string endpoint = kDefaultZmqEndpoint;
    int io_threads = 1;
    int sndhwm = 10000;
    bool bind = true;
};

class ZmqPublisher {
public:
    ZmqPublisher();
    ~ZmqPublisher();

    ZmqPublisher(const ZmqPublisher&) = delete;
    ZmqPublisher& operator=(const ZmqPublisher&) = delete;

    bool start(const ZmqPublisherConfig& config = ZmqPublisherConfig{});
    void stop();
    bool is_running() const { return running_; }

    bool publish(const Event& event);
    const std::string& endpoint() const { return config_.endpoint; }

private:
    ZmqPublisherConfig config_;
    void* context_{nullptr};
    void* socket_{nullptr};
    bool running_{false};
};

} // namespace neurofocus
