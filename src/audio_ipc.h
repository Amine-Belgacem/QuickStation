#pragma once
#include <windows.h>
#include <cstdint>

namespace radio::ipc {
// Private anonymous mapping, inherited only by the one playback process.
// The request is immutable; volume is interlocked; reports use the inherited mutex.
constexpr uint32_t Version = 5;
enum class State : uint32_t { Starting, Playing, Buffering, Retry, Error };
struct Report {
    ULONGLONG heartbeat = 0;
    State state = State::Starting;
    int error = 0;
    int pluginError = 0;
    uint16_t level = 0; // Post-volume peak, 0..32768.
    char title[4096]{};
};
struct Shared {
    uint32_t version = Version;
    bool silent = false;
    int device = -1;
    wchar_t url[4097]{};
    volatile LONG volume = 65;
    Report report;
};
class Handle {
  public:
    HANDLE value = nullptr;
    Handle() = default;
    explicit Handle(HANDLE handle) : value(handle) {}
    ~Handle() {
        if (value)
            CloseHandle(value);
    }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};
} // namespace radio::ipc
