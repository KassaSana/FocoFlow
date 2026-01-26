/**
 * Neural Focus: Memory-Mapped Logger
 */

#include "mmap_logger.h"
#include <windows.h>
#include <cstdio>
#include <cstring>

namespace neurofocus {
namespace {

constexpr uint32_t kMagic = 0x4C47464E;   // "NFGL"
constexpr uint32_t kVersion = 1;

struct LogHeader {
    uint32_t magic;
    uint32_t version;
    uint64_t write_offset;
    uint64_t event_count;
    uint64_t file_size;
    uint64_t reserved[4];
};

static_assert(sizeof(LogHeader) == 64, "LogHeader must be 64 bytes");

} // namespace

MmapLogger::MmapLogger() = default;

MmapLogger::~MmapLogger() {
    close();
}

bool MmapLogger::open(const MmapLoggerConfig& config) {
    close();
    config_ = config;

    if (config_.max_file_bytes < sizeof(LogHeader) + sizeof(Event)) {
        return false;
    }

    file_index_ = 0;
    current_day_ = current_day();
    return open_for_day(current_day_, true);
}

void MmapLogger::close() {
    unmap_file();
    current_path_.clear();
    current_day_ = 0;
    file_index_ = 0;
    write_offset_ = 0;
    mapped_size_ = 0;
    events_written_ = 0;
}

bool MmapLogger::is_open() const {
    return mapped_ != nullptr;
}

bool MmapLogger::append(const Event& event) {
    if (!mapped_) {
        return false;
    }

    if (config_.rotate_daily) {
        uint32_t today = current_day();
        if (today != current_day_) {
            file_index_ = 0;
            if (!open_for_day(today, true)) {
                return false;
            }
        }
    }

    if (write_offset_ + sizeof(Event) > mapped_size_) {
        if (!open_next_index(current_day_)) {
            return false;
        }
    }

    std::memcpy(mapped_ + write_offset_, &event, sizeof(Event));
    write_offset_ += sizeof(Event);
    events_written_++;

    auto* header = reinterpret_cast<LogHeader*>(mapped_);
    header->write_offset = write_offset_;
    header->event_count = events_written_;

    return true;
}

void MmapLogger::flush() {
    if (!mapped_ || !file_handle_) {
        return;
    }

    FlushViewOfFile(mapped_, 0);
    FlushFileBuffers(static_cast<HANDLE>(file_handle_));
}

uint64_t MmapLogger::capacity_events() const {
    if (mapped_size_ <= sizeof(LogHeader)) {
        return 0;
    }
    return (mapped_size_ - sizeof(LogHeader)) / sizeof(Event);
}

bool MmapLogger::open_for_day(uint32_t yyyymmdd, bool allow_existing) {
    unmap_file();
    current_day_ = yyyymmdd;
    current_path_ = build_path(yyyymmdd, file_index_);

    DWORD disposition = allow_existing ? OPEN_ALWAYS : CREATE_NEW;
    HANDLE file = CreateFileA(
        current_path_.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        disposition,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    file_handle_ = file;
    return map_file();
}

bool MmapLogger::open_next_index(uint32_t yyyymmdd) {
    for (uint32_t attempt = 0; attempt < 1000; ++attempt) {
        ++file_index_;
        current_path_ = build_path(yyyymmdd, file_index_);

        HANDLE file = CreateFileA(
            current_path_.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ,
            nullptr,
            CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);

        if (file == INVALID_HANDLE_VALUE) {
            DWORD err = GetLastError();
            if (err == ERROR_FILE_EXISTS) {
                continue;
            }
            return false;
        }

        unmap_file();
        file_handle_ = file;
        current_day_ = yyyymmdd;
        return map_file();
    }

    return false;
}

bool MmapLogger::map_file() {
    HANDLE file = static_cast<HANDLE>(file_handle_);
    if (file == INVALID_HANDLE_VALUE || file == nullptr) {
        return false;
    }

    LARGE_INTEGER size;
    if (!GetFileSizeEx(file, &size)) {
        return false;
    }

    bool needs_resize = false;
    if (size.QuadPart == 0) {
        size.QuadPart = static_cast<LONGLONG>(config_.max_file_bytes);
        needs_resize = true;
    } else if (size.QuadPart < static_cast<LONGLONG>(sizeof(LogHeader) + sizeof(Event))) {
        size.QuadPart = static_cast<LONGLONG>(config_.max_file_bytes);
        needs_resize = true;
    }

    if (needs_resize) {
        LARGE_INTEGER move;
        move.QuadPart = size.QuadPart;
        if (!SetFilePointerEx(file, move, nullptr, FILE_BEGIN)) {
            return false;
        }
        if (!SetEndOfFile(file)) {
            return false;
        }
    }

    mapping_handle_ = CreateFileMappingA(
        file,
        nullptr,
        PAGE_READWRITE,
        static_cast<DWORD>(size.QuadPart >> 32),
        static_cast<DWORD>(size.QuadPart & 0xFFFFFFFF),
        nullptr);

    if (!mapping_handle_) {
        return false;
    }

    mapped_ = static_cast<uint8_t*>(
        MapViewOfFile(static_cast<HANDLE>(mapping_handle_), FILE_MAP_WRITE | FILE_MAP_READ, 0, 0, 0));
    if (!mapped_) {
        CloseHandle(static_cast<HANDLE>(mapping_handle_));
        mapping_handle_ = nullptr;
        return false;
    }

    mapped_size_ = static_cast<uint64_t>(size.QuadPart);

    if (!init_or_load_header()) {
        return false;
    }

    return true;
}

void MmapLogger::unmap_file() {
    if (mapped_) {
        FlushViewOfFile(mapped_, 0);
        UnmapViewOfFile(mapped_);
        mapped_ = nullptr;
    }

    if (mapping_handle_) {
        CloseHandle(static_cast<HANDLE>(mapping_handle_));
        mapping_handle_ = nullptr;
    }

    if (file_handle_) {
        CloseHandle(static_cast<HANDLE>(file_handle_));
        file_handle_ = nullptr;
    }
}

bool MmapLogger::init_or_load_header() {
    if (!mapped_ || mapped_size_ < sizeof(LogHeader)) {
        return false;
    }

    auto* header = reinterpret_cast<LogHeader*>(mapped_);
    if (header->magic == kMagic &&
        header->version == kVersion &&
        header->file_size == mapped_size_ &&
        header->write_offset >= sizeof(LogHeader) &&
        header->write_offset <= mapped_size_ &&
        header->write_offset % sizeof(Event) == 0) {
        write_offset_ = header->write_offset;
        events_written_ = header->event_count;
        return true;
    }

    std::memset(header, 0, sizeof(LogHeader));
    header->magic = kMagic;
    header->version = kVersion;
    header->file_size = mapped_size_;
    header->write_offset = sizeof(LogHeader);
    header->event_count = 0;

    write_offset_ = header->write_offset;
    events_written_ = 0;
    return true;
}

uint32_t MmapLogger::current_day() const {
    SYSTEMTIME st;
    GetLocalTime(&st);
    return static_cast<uint32_t>(st.wYear * 10000 + st.wMonth * 100 + st.wDay);
}

std::string MmapLogger::build_path(uint32_t yyyymmdd, uint32_t index) const {
    int year = static_cast<int>(yyyymmdd / 10000);
    int month = static_cast<int>((yyyymmdd / 100) % 100);
    int day = static_cast<int>(yyyymmdd % 100);

    char name[128];
    if (index == 0) {
        std::snprintf(name, sizeof(name), "%s_%04d-%02d-%02d.log",
                      config_.base_name.c_str(), year, month, day);
    } else {
        std::snprintf(name, sizeof(name), "%s_%04d-%02d-%02d_%02u.log",
                      config_.base_name.c_str(), year, month, day, index);
    }

    std::string path = config_.directory;
    if (!path.empty()) {
        char last = path[path.size() - 1];
        if (last != '\\' && last != '/') {
            path.push_back('\\');
        }
    }

    path.append(name);
    return path;
}

} // namespace neurofocus
