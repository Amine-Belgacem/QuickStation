#pragma once
#include "core.h"
#include <cstdint>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace radio {
enum class PlaybackState { Stopped, Connecting, Playing, Buffering, Reconnecting, Error };
struct Playback {
    PlaybackState state = PlaybackState::Stopped;
    std::string stationId, title, detail = "Choose a favorite to listen";
    unsigned attempts = 0;
    uint16_t level = 0;
    bool operator==(const Playback&) const = default;
};
// A supervisor owns one disposable audio process. Commands replace one another.
// Snapshots cross the thread boundary by value; no HWND or UI object enters audio code.
class AudioPlayer {
  public:
    explicit AudioPlayer(bool silentDevice = false, fs::path diagnostics = {}, int outputDevice = -1);
    ~AudioPlayer();
    AudioPlayer(const AudioPlayer&) = delete;
    AudioPlayer& operator=(const AudioPlayer&) = delete;
    void play(const Station& station);
    void stop();
    void reset();
    void setVolume(int volume, bool mute);
    Playback snapshot() const;

  private:
    void run();
    void log(const char* event, uint64_t generation, int code = 0) noexcept;
    fs::path diagnostics_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::thread worker_;
    Station wanted_;
    Playback playback_;
    uint64_t generation_ = 0;
    bool closing_ = false, silentDevice_ = false;
    int volume_ = 65, outputDevice_ = -1;
};
} // namespace radio
