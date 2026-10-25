#include "audio.h"
#include "audio_ipc.h"
#include <bass.h>
#include <cctype>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace radio {
namespace {
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
std::string errorText(int error) {
    switch (error) {
    case 0:
        return "Connection lost";
    case BASS_ERROR_TIMEOUT:
        return "Station timed out";
    case BASS_ERROR_NONET:
        return "Network unavailable";
    case BASS_ERROR_FILEOPEN:
        return "Station unavailable";
    case BASS_ERROR_FILEFORM:
    case BASS_ERROR_CODEC:
    case BASS_ERROR_NOTAUDIO:
        return "Unsupported stream or codec";
    case BASS_ERROR_DENIED:
        return "Station requires authorization";
    case BASS_ERROR_SSL:
        return "Secure connection failed";
    case BASS_ERROR_DRIVER:
    case BASS_ERROR_DEVICE:
    case BASS_ERROR_INIT:
        return "Audio output device unavailable";
    default:
        return "Audio error " + std::to_string(error);
    }
}
bool formatError(int error) {
    return error == BASS_ERROR_FILEFORM || error == BASS_ERROR_CODEC || error == BASS_ERROR_NOTAUDIO;
}
bool hlsUrl(std::string_view url) {
    const auto end = url.find_first_of("?#");
    auto path = std::string(url.substr(0, end));
    std::transform(path.begin(), path.end(), path.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return path.ends_with(".m3u8");
}
// No BASS calls execute in the interface process, including Stop and shutdown.
// Only the helper inherits these anonymous handles; URLs never enter its command line.
class Session {
  public:
    ipc::Handle mapping, stop, mutex, job, process;
    ipc::Shared* shared = nullptr;
    explicit Session(const Station& station, bool silent, int device) {
        try {
            SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
            mapping.value = CreateFileMappingW(INVALID_HANDLE_VALUE, &security, PAGE_READWRITE, 0,
                                               sizeof(ipc::Shared), nullptr);
            stop.value = CreateEventW(&security, TRUE, FALSE, nullptr);
            mutex.value = CreateMutexW(&security, FALSE, nullptr);
            job.value = CreateJobObjectW(nullptr, nullptr);
            if (!mapping.value || !stop.value || !mutex.value || !job.value)
                throw std::runtime_error("Cannot allocate audio process resources");
            shared = static_cast<ipc::Shared*>(
                MapViewOfFile(mapping.value, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(ipc::Shared)));
            if (!shared)
                throw std::runtime_error("Cannot map audio process resources");
            new (shared) ipc::Shared{};
            shared->silent = silent;
            shared->device = device;
            // The supervisor arms volume only after validating the current command.
            shared->volume = 0;
            wcscpy_s(shared->url, wide(station.url).c_str());
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
            limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            if (!SetInformationJobObject(job.value, JobObjectExtendedLimitInformation, &limits,
                                         sizeof(limits)))
                throw std::runtime_error("Cannot supervise audio process");
            wchar_t executable[32768]{};
            if (!GetModuleFileNameW(nullptr, executable, 32768))
                throw std::runtime_error("Cannot locate audio process");
            auto path = fs::path(executable).parent_path() / L"QuickStationAudio.exe";
            std::wstring command = L"\"" + path.wstring() + L"\"";
            HANDLE inherited[] = {mapping.value, stop.value, mutex.value};
            for (auto handle : inherited)
                command += L" " + std::to_wstring(reinterpret_cast<uintptr_t>(handle));
            SIZE_T bytes = 0;
            InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
            std::vector<unsigned char> storage(bytes);
            auto* attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
            if (!InitializeProcThreadAttributeList(attributes, 1, 0, &bytes))
                throw std::runtime_error("Cannot prepare audio process");
            STARTUPINFOEXW startup{};
            startup.StartupInfo.cb = sizeof(startup);
            startup.lpAttributeList = attributes;
            PROCESS_INFORMATION info{};
            const bool launched =
                UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited,
                                          sizeof(inherited), nullptr, nullptr) &&
                CreateProcessW(path.c_str(), command.data(), nullptr, nullptr, TRUE,
                               CREATE_SUSPENDED | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr,
                               path.parent_path().c_str(), &startup.StartupInfo, &info);
            DeleteProcThreadAttributeList(attributes);
            if (!launched)
                throw std::runtime_error(
                    "Cannot launch QuickStationAudio.exe; keep it beside QuickStation.exe");
            process.value = info.hProcess;
            ipc::Handle thread(info.hThread);
            if (!AssignProcessToJobObject(job.value, process.value) ||
                ResumeThread(thread.value) == static_cast<DWORD>(-1)) {
                TerminateProcess(process.value, 1);
                WaitForSingleObject(process.value, 200);
                throw std::runtime_error("Cannot start supervised audio process");
            }
        } catch (...) {
            if (shared) {
                UnmapViewOfFile(shared);
                shared = nullptr;
            }
            throw;
        }
    }
    ~Session() {
        // Closing the job is a final backstop if the application itself exits.
        if (process.value && WaitForSingleObject(process.value, 0) == WAIT_TIMEOUT)
            TerminateJobObject(job.value, 1);
        if (shared)
            UnmapViewOfFile(shared);
    }
    bool finish() {
        SetEvent(stop.value);
        if (WaitForSingleObject(process.value, 150) == WAIT_OBJECT_0)
            return true;
        TerminateJobObject(job.value, 1);
        return WaitForSingleObject(process.value, 250) == WAIT_OBJECT_0;
    }
    bool read(ipc::Report& report) {
        const auto result = WaitForSingleObject(mutex.value, 0);
        if (result != WAIT_OBJECT_0 && result != WAIT_ABANDONED)
            return false;
        if (result == WAIT_OBJECT_0)
            report = shared->report;
        ReleaseMutex(mutex.value);
        return result == WAIT_OBJECT_0;
    }
};
} // namespace
AudioPlayer::AudioPlayer(bool silent, fs::path diagnostics, int outputDevice)
    : diagnostics_(std::move(diagnostics)), silentDevice_(silent), outputDevice_(outputDevice) {
    worker_ = std::thread([this] { run(); });
}
AudioPlayer::~AudioPlayer() {
    {
        std::lock_guard lock(mutex_);
        closing_ = true;
        ++generation_;
    }
    wake_.notify_one();
    if (worker_.joinable())
        worker_.join();
}
void AudioPlayer::play(const Station& station) {
    validate(station);
    {
        std::lock_guard lock(mutex_);
        wanted_ = station;
        ++generation_;
        playback_ = {PlaybackState::Connecting, station.id, "", "Connecting…", 0};
    }
    wake_.notify_one();
}
void AudioPlayer::stop() {
    {
        std::lock_guard lock(mutex_);
        wanted_ = {};
        ++generation_;
        playback_ = {};
    }
    wake_.notify_one();
}
void AudioPlayer::reset() {
    {
        std::lock_guard lock(mutex_);
        ++generation_;
        playback_ = wanted_.url.empty()
                        ? Playback{}
                        : Playback{PlaybackState::Connecting, wanted_.id, "", "Restarting playback…", 0};
    }
    wake_.notify_one();
}
void AudioPlayer::setVolume(int volume, bool mute) {
    {
        std::lock_guard lock(mutex_);
        volume_ = mute ? 0 : std::clamp(volume, 0, 100);
        if (volume_ == 0)
            playback_.level = 0;
    }
    wake_.notify_one();
}
Playback AudioPlayer::snapshot() const {
    std::lock_guard lock(mutex_);
    return playback_;
}
void AudioPlayer::log(const char* event, uint64_t generation, int code) noexcept {
    // Deliberately omit station names, URLs, metadata and arbitrary exception text.
    // One capped file, no cloud reporting, no credentials or query tokens.
    if (diagnostics_.empty())
        return;
    try {
        std::error_code ec;
        const auto size = fs::file_size(diagnostics_, ec);
        std::ofstream file(diagnostics_,
                           std::ios::binary | (!ec && size > 64 * 1024 ? std::ios::trunc : std::ios::app));
        file << GetTickCount64() << " generation=" << generation << " " << event << " code=" << code << '\n';
    } catch (...) {
    }
}
void AudioPlayer::run() {
    std::unique_ptr<Session> session;
    uint64_t applied = UINT64_MAX;
    Station current;
    unsigned failures = 0, formatFailures = 0;
    bool blocked = false, started = false;
    bool pluginReported = false;
    ipc::Report report{};
    auto next = Clock::now(), launched = next, stalled = Clock::time_point{}, stable = stalled;
    for (;;) {
        try {
            std::unique_lock lock(mutex_);
            if (closing_)
                break;
            if (generation_ != applied) {
                const auto requested = generation_;
                lock.unlock();
                // Never launch a replacement until the previous process has actually exited.
                const bool ended = !session || session->finish();
                log(ended ? "command-replaced" : "awaiting-process-exit", requested);
                if (ended)
                    session.reset();
                lock.lock();
                if (closing_)
                    break;
                if (!ended) {
                    if (!wanted_.url.empty()) {
                        playback_.state = PlaybackState::Reconnecting;
                        playback_.detail = "Waiting for the previous audio process to exit";
                        playback_.level = 0;
                    }
                    wake_.wait_for(lock, 250ms);
                    continue;
                }
                if (requested != generation_)
                    continue;
                applied = generation_;
                current = wanted_;
                failures = 0;
                formatFailures = 0;
                blocked = false;
                pluginReported = false;
                started = false;
                stalled = {};
                stable = {};
                next = Clock::now();
            }
            if (current.url.empty() || blocked) {
                wake_.wait(lock, [&] { return closing_ || generation_ != applied; });
                continue;
            }
            if (!session && Clock::now() >= next) {
                playback_ = {failures ? PlaybackState::Reconnecting : PlaybackState::Connecting, current.id,
                             "", failures ? "Reconnecting…" : "Connecting…", failures};
                lock.unlock();
                try {
                    session = std::make_unique<Session>(current, silentDevice_, outputDevice_);
                } catch (const std::exception& error) {
                    log("launch-failed", applied);
                    lock.lock();
                    if (closing_ || generation_ != applied)
                        continue;
                    const auto delay = retryDelay(failures++);
                    next = Clock::now() + delay;
                    playback_ = {PlaybackState::Reconnecting, current.id, "",
                                 std::string(error.what()) + " · Retry in " + std::to_string(delay.count()) +
                                     "s",
                                 failures};
                    continue;
                }
                report = {};
                pluginReported = false;
                log("attempt-started", applied);
                launched = Clock::now();
                started = false;
                stalled = {};
                stable = {};
                lock.lock();
                if (closing_ || generation_ != applied)
                    continue;
            }
            if (session) {
                InterlockedExchange(&session->shared->volume, volume_);
                session->read(report);
                if (report.pluginError && !pluginReported) {
                    log("hls-plugin-load-failed", applied, report.pluginError);
                    pluginReported = true;
                }
                const auto now = Clock::now();
                std::string failure;
                bool permanent = false;
                // A total startup deadline includes initialization, playlists and decoder probing.
                if (!started && now - launched >= 15s)
                    failure = "Connection startup timed out";
                else if (started && (report.heartbeat == 0 || GetTickCount64() - report.heartbeat > 5000))
                    failure = "Audio process stopped responding";
                else if (report.state == ipc::State::Retry || report.state == ipc::State::Error) {
                    failure = report.pluginError && hlsUrl(current.url) && formatError(report.error)
                                  ? "HLS decoder unavailable (plugin error " +
                                        std::to_string(report.pluginError) + ")"
                                  : errorText(report.error);
                    permanent = report.state == ipc::State::Error;
                } else if (WaitForSingleObject(session->process.value, 0) == WAIT_OBJECT_0) {
                    DWORD exitCode{};
                    GetExitCodeProcess(session->process.value, &exitCode);
                    failure = exitCode == 0xc0000135UL
                                  ? "Audio dependency missing; extract the complete application ZIP"
                              : exitCode == 0xc000007bUL
                                  ? "Audio dependency invalid; extract the complete x64 application ZIP"
                                  : "Audio process exited unexpectedly";
                } else if (report.state == ipc::State::Playing) {
                    started = true;
                    formatFailures = 0;
                    stalled = {};
                    if (stable == Clock::time_point{})
                        stable = now;
                    if (now - stable >= 30s)
                        failures = 0;
                    playback_ = {PlaybackState::Playing, current.id,
                                 std::string(report.title, strnlen_s(report.title, sizeof(report.title))),
                                 "Connected · Auto reconnect on", failures,
                                 volume_ > 0 && report.heartbeat &&
                                         GetTickCount64() - report.heartbeat <= 500
                                     ? report.level
                                     : uint16_t{0}};
                } else if (report.state == ipc::State::Buffering) {
                    stable = {};
                    if (stalled == Clock::time_point{})
                        stalled = now;
                    playback_.state = PlaybackState::Buffering;
                    playback_.detail = "Buffering… · Auto reconnect on";
                    playback_.level = 0;
                    if (now - stalled >= 12s)
                        failure = "Stream stalled";
                }
                if (!failure.empty()) {
                    lock.unlock();
                    const bool ended = session->finish();
                    log(failure.c_str(), applied, report.error);
                    if (ended)
                        session.reset();
                    lock.lock();
                    if (closing_ || generation_ != applied)
                        continue;
                    // If Windows has not completed termination, retain ownership and keep checking.
                    // Never accumulate abandoned processes, even during repeated recovery requests.
                    if (ended) {
                        blocked = permanent;
                        const bool repeatedFormat = formatError(report.error) && !started;
                        formatFailures = repeatedFormat ? formatFailures + 1 : 0;
                        const auto delay = formatFailures >= 3
                                               ? std::chrono::seconds(formatFailures >= 5 ? 300 : 120)
                                               : retryDelay(failures);
                        ++failures;
                        next = Clock::now() + delay;
                        playback_ = {
                            permanent ? PlaybackState::Error : PlaybackState::Reconnecting, current.id, "",
                            failure + (permanent ? " · Edit the URL or retry"
                                                 : formatFailures >= 3
                                                       ? " · Automatic probe in " +
                                                             std::to_string(delay.count()) + "s"
                                                       : " · Retry in " + std::to_string(delay.count()) + "s"),
                            failures};
                    } else {
                        playback_.state = PlaybackState::Reconnecting;
                        playback_.detail = "Waiting for the audio process to exit";
                        playback_.level = 0;
                    }
                }
            }
            wake_.wait_for(lock, 50ms);
        } catch (const std::exception&) {
            // Keep the supervisor alive after recoverable failures; subsequent commands still work.
            log("supervisor-recovery", applied);
            const bool ended = !session || session->finish();
            if (ended)
                session.reset();
            std::lock_guard lock(mutex_);
            if (closing_)
                break;
            if (generation_ != applied)
                continue;
            const auto delay = retryDelay(failures++);
            next = Clock::now() + delay;
            playback_ = {PlaybackState::Reconnecting, current.id, "",
                         "Playback interrupted · Retry in " + std::to_string(delay.count()) + "s", failures};
        }
    }
    if (session)
        session->finish();
}
} // namespace radio
