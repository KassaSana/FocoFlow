/**
 * Neural Focus: ZeroMQ Publisher
 */

#include "zmq_publisher.h"
#include <cstdio>
#include <cstring>
#include <zmq.h>

namespace neurofocus {

ZmqPublisher::ZmqPublisher() = default;

ZmqPublisher::~ZmqPublisher() {
    stop();
}

bool ZmqPublisher::start(const ZmqPublisherConfig& config) {
    if (running_) {
        return true;
    }

    config_ = config;

    context_ = zmq_ctx_new();
    if (!context_) {
        std::fprintf(stderr, "[ZmqPublisher] zmq_ctx_new failed\n");
        return false;
    }

    if (zmq_ctx_set(context_, ZMQ_IO_THREADS, config_.io_threads) != 0) {
        std::fprintf(stderr, "[ZmqPublisher] zmq_ctx_set failed: %s\n",
                     zmq_strerror(zmq_errno()));
        stop();
        return false;
    }

    socket_ = zmq_socket(context_, ZMQ_PUB);
    if (!socket_) {
        std::fprintf(stderr, "[ZmqPublisher] zmq_socket failed: %s\n",
                     zmq_strerror(zmq_errno()));
        stop();
        return false;
    }

    if (zmq_setsockopt(socket_, ZMQ_SNDHWM, &config_.sndhwm, sizeof(config_.sndhwm)) != 0) {
        std::fprintf(stderr, "[ZmqPublisher] zmq_setsockopt(SNDHWM) failed: %s\n",
                     zmq_strerror(zmq_errno()));
        stop();
        return false;
    }

    int rc = 0;
    if (config_.bind) {
        rc = zmq_bind(socket_, config_.endpoint.c_str());
    } else {
        rc = zmq_connect(socket_, config_.endpoint.c_str());
    }

    if (rc != 0) {
        std::fprintf(stderr, "[ZmqPublisher] endpoint %s failed: %s\n",
                     config_.endpoint.c_str(), zmq_strerror(zmq_errno()));
        stop();
        return false;
    }

    running_ = true;
    std::printf("[ZmqPublisher] Started at %s\n", config_.endpoint.c_str());
    return true;
}

void ZmqPublisher::stop() {
    if (socket_) {
        zmq_close(socket_);
        socket_ = nullptr;
    }

    if (context_) {
        zmq_ctx_term(context_);
        context_ = nullptr;
    }

    running_ = false;
}

bool ZmqPublisher::publish(const Event& event) {
    if (!running_ || !socket_) {
        return false;
    }

    zmq_msg_t msg;
    if (zmq_msg_init_size(&msg, sizeof(Event)) != 0) {
        return false;
    }

    std::memcpy(zmq_msg_data(&msg), &event, sizeof(Event));
    int rc = zmq_msg_send(&msg, socket_, ZMQ_DONTWAIT);
    zmq_msg_close(&msg);
    return rc == static_cast<int>(sizeof(Event));
}

} // namespace neurofocus
