/**
 * Neural Focus: Core Benchmark
 *
 * Measures ring buffer latency and end-to-end throughput.
 *
 * BUILD:
 *   g++ -std=c++17 benchmark.cpp -I .. -o benchmark.exe
 */

#include "core/event.h"
#include "core/ring_buffer.h"
#include <algorithm>
#include <memory>
#include <chrono>
#include <cstdio>
#include <vector>

namespace {

constexpr size_t kBufferSize = 65536;
constexpr uint32_t kBatchSize = 8192;
constexpr uint32_t kBatchSamples = 200;

uint64_t steady_ns() {
    auto now = std::chrono::steady_clock::now();
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        now.time_since_epoch()).count();
    return static_cast<uint64_t>(ns);
}

double percentile(std::vector<double> samples, double p) {
    if (samples.empty()) {
        return 0.0;
    }
    std::sort(samples.begin(), samples.end());
    size_t idx = static_cast<size_t>((p / 100.0) * (samples.size() - 1));
    return static_cast<double>(samples[idx]);
}

void report_latency(const char* label, const std::vector<double>& samples) {
    std::printf("%s: p50=%.2f ns  p99=%.2f ns  p999=%.2f ns  n=%zu\n",
                label,
                percentile(samples, 50.0),
                percentile(samples, 99.0),
                percentile(samples, 99.9),
                samples.size());
}

} // namespace

int main() {
    std::printf("==========================================\n");
    std::printf("  Neural Focus: Core Benchmark\n");
    std::printf("==========================================\n\n");

    auto buffer = std::make_unique<LockFreeRingBuffer<Event, kBufferSize>>();

    // Benchmark push latency (keep buffer mostly empty).
    std::vector<double> push_samples;
    push_samples.reserve(kBatchSamples);
    for (uint32_t batch = 0; batch < kBatchSamples; ++batch) {
        Event event{};
        uint64_t start = steady_ns();
        for (uint32_t i = 0; i < kBatchSize; ++i) {
            buffer->try_push(event);
        }
        uint64_t end = steady_ns();
        push_samples.push_back(static_cast<double>(end - start) / kBatchSize);

        Event tmp;
        for (uint32_t i = 0; i < kBatchSize; ++i) {
            buffer->try_pop(tmp);
        }
    }
    report_latency("try_push", push_samples);

    // Benchmark pop latency (fill once, then drain).
    std::vector<double> pop_samples;
    pop_samples.reserve(kBatchSamples);
    for (uint32_t batch = 0; batch < kBatchSamples; ++batch) {
        for (uint32_t i = 0; i < kBatchSize; ++i) {
            Event event{};
            buffer->try_push(event);
        }

        Event event{};
        uint64_t start = steady_ns();
        for (uint32_t i = 0; i < kBatchSize; ++i) {
            buffer->try_pop(event);
        }
        uint64_t end = steady_ns();
        pop_samples.push_back(static_cast<double>(end - start) / kBatchSize);
    }
    report_latency("try_pop ", pop_samples);

    // End-to-end latency (push then pop immediately).
    std::vector<double> e2e_samples;
    e2e_samples.reserve(kBatchSamples);
    for (uint32_t batch = 0; batch < kBatchSamples; ++batch) {
        uint64_t start = steady_ns();
        for (uint32_t i = 0; i < kBatchSize; ++i) {
            Event event{};
            buffer->try_push(event);
            buffer->try_pop(event);
        }
        uint64_t end = steady_ns();
        e2e_samples.push_back(static_cast<double>(end - start) / kBatchSize);
    }
    report_latency("end_to_end", e2e_samples);

    // Throughput test (single-thread push+pop for 2 seconds).
    uint64_t count = 0;
    auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::steady_clock::now() - t0).count() < 2) {
        Event event{};
        if (buffer->try_push(event) && buffer->try_pop(event)) {
            ++count;
        }
    }

    double seconds = 2.0;
    double rate = count / seconds;
    std::printf("throughput: %.0f ev/s (push+pop single-thread)\n", rate);

    return 0;
}
