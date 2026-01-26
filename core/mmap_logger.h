/**
 * Neural Focus: Memory-Mapped Logger
 *
 * Append-only event log using a memory-mapped file.
 * Single-producer by design; caller is responsible for threading.
 */

#pragma once

#include "event.h"
#include <cstdint>
#include <string>

namespace neurofocus {

struct MmapLoggerConfig {
    std::string directory = ".";
    std::string base_name = "events";
    uint64_t max_file_bytes = 256ull * 1024ull * 1024ull;
    bool rotate_daily = true;
};

class MmapLogger {
public:
    MmapLogger();
    ~MmapLogger();

    MmapLogger(const MmapLogger&) = delete;
    MmapLogger& operator=(const MmapLogger&) = delete;

    bool open(const MmapLoggerConfig& config = MmapLoggerConfig{});
    void close();
    bool is_open() const;

    bool append(const Event& event);
    void flush();

    uint64_t events_written() const { return events_written_; }
    uint64_t capacity_events() const;
    const std::string& current_path() const { return current_path_; }

private:
    bool open_for_day(uint32_t yyyymmdd, bool allow_existing);
    bool open_next_index(uint32_t yyyymmdd);
    bool map_file();
    void unmap_file();
    bool init_or_load_header();
    uint32_t current_day() const;
    std::string build_path(uint32_t yyyymmdd, uint32_t index) const;

    MmapLoggerConfig config_;
    std::string current_path_;
    uint32_t current_day_{0};
    uint32_t file_index_{0};

    void* file_handle_{nullptr};
    void* mapping_handle_{nullptr};
    uint8_t* mapped_{nullptr};
    uint64_t mapped_size_{0};
    uint64_t write_offset_{0};
    uint64_t events_written_{0};
};

} // namespace neurofocus
