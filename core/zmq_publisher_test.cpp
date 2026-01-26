/**
 * Neural Focus: ZeroMQ Publisher Test
 *
 * BUILD:
 *   g++ -std=c++17 zmq_publisher_test.cpp zmq_publisher.cpp -o zmq_publisher_test.exe -lzmq
 */

#include "zmq_publisher.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
#include <zmq.h>

static uint64_t now_us() {
    auto now = std::chrono::system_clock::now();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(
        now.time_since_epoch()).count();
    return static_cast<uint64_t>(us);
}

static neurofocus::Event make_event(uint32_t pid, uint32_t index) {
    neurofocus::Event event{};
    event.timestamp_us = now_us();
    event.event_type = EventType::KEY_PRESS;
    event.process_id = pid;
    std::snprintf(event.app_name, sizeof(event.app_name), "test_pub.exe");
    event.window_handle = 0x1234;
    event.data.key.virtual_key_code = 65 + (index % 26);
    event.data.key.scan_code = 0;
    event.data.key.flags = 0;
    event.reserved = 0;
    return event;
}

int main() {
    const char* endpoint = "tcp://127.0.0.1:5556";

    void* sub_ctx = zmq_ctx_new();
    void* sub = zmq_socket(sub_ctx, ZMQ_SUB);
    const char* empty = "";
    zmq_setsockopt(sub, ZMQ_SUBSCRIBE, empty, 0);
    if (zmq_connect(sub, endpoint) != 0) {
        std::fprintf(stderr, "SUB connect failed: %s\n", zmq_strerror(zmq_errno()));
        return 1;
    }

    neurofocus::ZmqPublisher publisher;
    neurofocus::ZmqPublisherConfig config;
    config.endpoint = endpoint;
    config.bind = true;
    config.sndhwm = 1000;

    if (!publisher.start(config)) {
        std::fprintf(stderr, "Publisher start failed\n");
        return 1;
    }

    // Allow subscription to propagate (slow joiner).
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    const uint32_t pid = 4242;
    for (uint32_t i = 0; i < 10; ++i) {
        neurofocus::Event event = make_event(pid, i);
        publisher.publish(event);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    zmq_pollitem_t items[] = {{sub, 0, ZMQ_POLLIN, 0}};
    int received = 0;

    auto start = std::chrono::steady_clock::now();
    while (received < 1) {
        zmq_poll(items, 1, 100);
        if (items[0].revents & ZMQ_POLLIN) {
            zmq_msg_t msg;
            zmq_msg_init(&msg);
            int rc = zmq_msg_recv(&msg, sub, 0);
            if (rc == static_cast<int>(sizeof(neurofocus::Event))) {
                received++;
            }
            zmq_msg_close(&msg);
        }

        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - start).count() > 2) {
            break;
        }
    }

    publisher.stop();
    zmq_close(sub);
    zmq_ctx_term(sub_ctx);

    if (received < 1) {
        std::fprintf(stderr, "No events received\n");
        return 1;
    }

    std::printf("Received %d event(s)\n", received);
    return 0;
}
