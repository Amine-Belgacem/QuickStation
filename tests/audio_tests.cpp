#include "audio.h"
#include <atomic>
#include <cstring>
#include <functional>
#include <fstream>
#include <iostream>
#include <thread>
#include <winsock2.h>
#include <tlhelp32.h>
#include <ws2tcpip.h>

using namespace radio;
using namespace std::chrono_literals;
// Local, deterministic radio server: infinite WAV, ICY title blocks, and an
// intentional disconnect. No internet dependency and no audible test playback.
class Server {
  public:
    enum class Mode { Live, Headers, Audio, Dribble, Hls };
    Mode mode;
    std::atomic<int> closed{0};
    SOCKET socket = INVALID_SOCKET;
    unsigned short port = 0;
    std::atomic<int> connections{0};
    std::atomic<bool> fail{false}, badContent{false};
    std::jthread thread;
    explicit Server(Mode value = Mode::Live) : mode(value) {
        socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (bind(socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR ||
            listen(socket, 4) == SOCKET_ERROR)
            throw std::runtime_error("Test server failed");
        int size = sizeof(addr);
        getsockname(socket, reinterpret_cast<sockaddr*>(&addr), &size);
        port = ntohs(addr.sin_port);
        thread = std::jthread([this](std::stop_token stop) { serve(stop); });
    }
    ~Server() {
        thread.request_stop();
        closesocket(socket);
        if (thread.joinable())
            thread.join();
    }
    void hold(SOCKET client, std::stop_token stop, bool dribble = false) {
        u_long nonblocking = 1;
        ioctlsocket(client, FIONBIO, &nonblocking);
        auto next = std::chrono::steady_clock::now();
        while (!stop.stop_requested()) {
            char byte = 0;
            const int result = recv(client, &byte, 1, MSG_PEEK);
            if (result == 0 || (result == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK))
                break;
            if (dribble && std::chrono::steady_clock::now() >= next) {
                if (send(client, &byte, 1, 0) <= 0)
                    break;
                next = std::chrono::steady_clock::now() + 100ms;
            }
            std::this_thread::sleep_for(20ms);
        }
        closesocket(client);
        ++closed;
    }
    void serve(std::stop_token stop) {
        while (!stop.stop_requested()) {
            SOCKET client = accept(socket, nullptr, nullptr);
            if (client == INVALID_SOCKET)
                break;
            ++connections;
            DWORD timeout = 500;
            setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<char*>(&timeout), sizeof(timeout));
            setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char*>(&timeout), sizeof(timeout));
            char request[4096]{};
            recv(client, request, sizeof(request) - 1, 0);
            if (mode == Mode::Headers) {
                hold(client, stop);
                continue;
            }
            if (mode == Mode::Hls) {
                if (std::string_view(request).find("/segment.aac") != std::string_view::npos) {
                    hold(client, stop);
                    continue;
                }
                const std::string body = "#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-TARGETDURATION:10\n#EXT-X-MEDIA-"
                                         "SEQUENCE:0\n#EXTINF:10,\nhttp://127.0.0.1:" +
                                         std::to_string(port) + "/segment.aac\n";
                const std::string response =
                    "HTTP/1.0 200 OK\r\nContent-Type: application/vnd.apple.mpegurl\r\nContent-Length: " +
                    std::to_string(body.size()) + "\r\n\r\n" + body;
                send(client, response.data(), static_cast<int>(response.size()), 0);
                closesocket(client);
                ++closed;
                continue;
            }
            if (fail) {
                const char* response = "HTTP/1.0 503 Unavailable\r\nContent-Length: 0\r\n\r\n";
                send(client, response, static_cast<int>(strlen(response)), 0);
                closesocket(client);
                continue;
            }
            if (badContent) {
                const std::string body =
                    "<html>Station temporarily unavailable</html>" + std::string(2048, ' ');
                const auto response = "HTTP/1.0 200 OK\r\nContent-Type: text/html\r\nContent-Length: " +
                                      std::to_string(body.size()) + "\r\n\r\n" + body;
                send(client, response.data(), static_cast<int>(response.size()), 0);
                closesocket(client);
                ++closed;
                continue;
            }
            const char* headers = "HTTP/1.0 200 OK\r\nContent-Type: audio/wav\r\nicy-name: Test "
                                  "Radio\r\nicy-metaint: 4096\r\n\r\n";
            if (mode != Mode::Live)
                headers = "HTTP/1.0 200 OK\r\nContent-Type: audio/wav\r\n\r\n";
            send(client, headers, static_cast<int>(strlen(headers)), 0);
            std::string wave(4096, '\0');
            memcpy(wave.data(), "RIFF", 4);
            uint32_t value = 0x7fffffff;
            memcpy(wave.data() + 4, &value, 4);
            memcpy(wave.data() + 8, "WAVEfmt ", 8);
            value = 16;
            memcpy(wave.data() + 16, &value, 4);
            uint16_t small = 1;
            memcpy(wave.data() + 20, &small, 2);
            memcpy(wave.data() + 22, &small, 2);
            value = 8000;
            memcpy(wave.data() + 24, &value, 4);
            value = 16000;
            memcpy(wave.data() + 28, &value, 4);
            small = 2;
            memcpy(wave.data() + 32, &small, 2);
            small = 16;
            memcpy(wave.data() + 34, &small, 2);
            memcpy(wave.data() + 36, "data", 4);
            value = 0x7fffffff - 36;
            memcpy(wave.data() + 40, &value, 4);
            if (mode == Mode::Audio || mode == Mode::Dribble) {
                if (mode == Mode::Dribble)
                    send(client, wave.data(), 44, 0);
                hold(client, stop, mode == Mode::Dribble);
                continue;
            }
            auto fillTone = [](std::string& data, size_t start) {
                for (size_t offset = start; offset + sizeof(int16_t) <= data.size(); offset += 2) {
                    const int16_t sample = (offset / 2) & 1 ? 8000 : -8000;
                    memcpy(data.data() + offset, &sample, sizeof(sample));
                }
            };
            fillTone(wave, 44);
            bool first = true;
            for (int i = 0; !stop.stop_requested() && !fail; ++i) {
                if (send(client, wave.data(), static_cast<int>(wave.size()), 0) <= 0)
                    break;
                if (first) {
                    std::string title = "StreamTitle='Test Artist - Test Song';";
                    title.resize(48, '\0');
                    char count = 3;
                    send(client, &count, 1, 0);
                    send(client, title.data(), 48, 0);
                    first = false;
                    wave.assign(4096, '\0');
                    fillTone(wave, 0);
                } else {
                    char zero = 0;
                    send(client, &zero, 1, 0);
                }
                std::this_thread::sleep_for(80ms);
            }
            shutdown(client, SD_BOTH);
            closesocket(client);
            ++closed;
        }
    }
};
bool waitFor(const std::function<bool()>& fn, std::chrono::milliseconds timeout) {
    auto end = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < end) {
        if (fn())
            return true;
        std::this_thread::sleep_for(50ms);
    }
    return false;
}
int checks = 0;
void require(bool ok, const char* text) {
    if (!ok)
        throw std::runtime_error(text);
    ++checks;
    std::cout << "PASS: " << text << '\n';
}
// Enumerate only this test process's own supervised decoder, never unrelated apps.
HANDLE decoderProcess(DWORD access) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return nullptr;
    PROCESSENTRY32W entry{sizeof(entry)};
    HANDLE process = nullptr;
    if (Process32FirstW(snapshot, &entry))
        do {
            if (entry.th32ParentProcessID == GetCurrentProcessId() &&
                std::wstring_view(entry.szExeFile) == L"QuickStationAudio.exe") {
                process = OpenProcess(access, FALSE, entry.th32ProcessID);
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    CloseHandle(snapshot);
    return process;
}
int runningDecoders() {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        throw std::runtime_error("Cannot enumerate test decoders");
    PROCESSENTRY32W entry{sizeof(entry)};
    int count = 0;
    if (Process32FirstW(snapshot, &entry))
        do {
            if (entry.th32ParentProcessID == GetCurrentProcessId() &&
                std::wstring_view(entry.szExeFile) == L"QuickStationAudio.exe") {
                HANDLE child = OpenProcess(SYNCHRONIZE, FALSE, entry.th32ProcessID);
                if (child) {
                    if (WaitForSingleObject(child, 0) == WAIT_TIMEOUT)
                        ++count;
                    CloseHandle(child);
                }
            }
        } while (Process32NextW(snapshot, &entry));
    CloseHandle(snapshot);
    return count;
}
int wmain(int argc, wchar_t** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    WSADATA data{};
    WSAStartup(MAKEWORD(2, 2), &data);
    try {
        if (argc == 3 && std::wstring_view(argv[1]) == L"--launch-retry") {
            Server server;
            AudioPlayer player(true);
            player.play({newId(), "Launch recovery",
                         "http://127.0.0.1:" + std::to_string(server.port) + "/live", "", "", true});
            require(waitFor([&] { return player.snapshot().state == PlaybackState::Reconnecting; }, 3s),
                    "Missing helper triggers automatic retry");
            wchar_t current[32768]{};
            GetModuleFileNameW(nullptr, current, 32768);
            const auto destination = fs::path(current).parent_path();
            fs::copy_file(fs::path(argv[2]) / L"QuickStationAudio.exe",
                          destination / L"QuickStationAudio.exe");
            require(waitFor(
                        [&] {
                            return player.snapshot().detail.find("Audio dependency missing") !=
                                   std::string::npos;
                        },
                        5s),
                    "Missing decoder DLL gives actionable status without a loader dialog");
            for (const auto* name : {L"bass.dll", L"basshls.dll"})
                fs::copy_file(fs::path(argv[2]) / name, destination / name);
            require(waitFor([&] { return player.snapshot().state == PlaybackState::Playing; }, 8s),
                    "Helper restoration recovers without Play or reset");
            return 0;
        }
        if (argc == 2 && std::wstring_view(argv[1]) == L"--plugin-check") {
            Server server;
            const auto diagnostic = fs::current_path() / "plugin-test.log";
            AudioPlayer player(true, diagnostic);
            player.play({newId(), "Native stream",
                         "http://127.0.0.1:" + std::to_string(server.port) + "/live", "", "", true});
            require(waitFor([&] { return player.snapshot().state == PlaybackState::Playing; }, 8s),
                    "Normal stream plays when HLS plugin is unavailable");
            require(waitFor([&] {
                        if (!fs::exists(diagnostic))
                            return false;
                        return readFile(diagnostic).find("hls-plugin-load-failed code=") !=
                               std::string::npos;
                    }, 4s),
                    "HLS plugin load error is captured before stream probing overwrites it");
            player.stop();
            return 0;
        }
        if (argc == 2 && std::wstring_view(argv[1]) == L"--probe") {
            wchar_t buffer[4097]{};
            const auto length = GetEnvironmentVariableW(L"QUICKSTATION_PROBE_URL", buffer, 4097);
            if (!length || length >= 4097)
                throw std::runtime_error("Set QUICKSTATION_PROBE_URL for the isolated probe.");
            Station station{newId(), "Probe", utf8(std::wstring_view(buffer, length)), "", "", true};
            validate(station);
            const auto log = fs::current_path() / "probe-playback.log";
            AudioPlayer player(true, log);
            player.play(station);
            auto previous = PlaybackState::Stopped;
            Playback result;
            for (int i = 0; i < 100; ++i) {
                result = player.snapshot();
                if (result.state != previous) {
                    std::cout << "state=" << static_cast<int>(result.state) << " detail=" << result.detail
                              << '\n';
                    previous = result.state;
                }
                if (result.state == PlaybackState::Playing)
                    break;
                std::this_thread::sleep_for(250ms);
            }
            wchar_t hold[8]{};
            const auto holdLength = GetEnvironmentVariableW(L"QUICKSTATION_PROBE_HOLD", hold, 8);
            if (result.state == PlaybackState::Playing && holdLength > 0 && holdLength < 8) {
                const auto seconds = std::clamp(_wtoi(hold), 0, 60);
                for (int i = 0; i < seconds * 4; ++i) {
                    std::this_thread::sleep_for(250ms);
                    result = player.snapshot();
                    if (result.state != PlaybackState::Playing)
                        break;
                }
                std::cout << "heldState=" << static_cast<int>(result.state)
                          << " durationSeconds=" << seconds << '\n';
            }
            player.stop();
            if (fs::exists(log))
                std::cout << "diagnostics:\n" << readFile(log) << '\n';
            return result.state == PlaybackState::Playing ? 0 : 3;
        }
        const auto diagnostics = fs::temp_directory_path() / ("quickstation-audio-" + newId() + ".log");
        {
            std::ofstream oversized(diagnostics);
            oversized << std::string(70 * 1024, 'x');
        }
        {
            AudioPlayer player(false, {}, 100000);
            player.play({newId(), "Invalid output device", "http://127.0.0.1:1/stream", "", "", true});
            const bool deviceError = waitFor([&] {
                return player.snapshot().detail.find("Audio output device unavailable") !=
                       std::string::npos;
            }, 12s);
            if (!deviceError) {
                const auto status = player.snapshot();
                throw std::runtime_error("Expected invalid-device status; saw: " + status.detail);
            }
            require(deviceError, "BASS_Init failure code reaches the supervisor's visible status");
            player.stop();
            require(waitFor([&] { return runningDecoders() == 0; }, 2s),
                    "Invalid audio device stops without retaining a helper");
        }
        {
            Server server;
            AudioPlayer player(true, diagnostics);
            Station a{newId(),
                      "Test",
                      "http://127.0.0.1:" + std::to_string(server.port) + "/live?token=do-not-log-this",
                      "",
                      "",
                      true};
            player.setVolume(0, true);
            player.play(a);
            require(waitFor([&] { return player.snapshot().state == PlaybackState::Playing; }, 8s),
                    "Real decoder connects to local stream");
            require(waitFor([&] { return player.snapshot().title == "Test Artist - Test Song"; }, 4s),
                    "ICY metadata reaches snapshot");
            player.setVolume(65, false);
            require(waitFor([&] { return player.snapshot().level > 0; }, 4s),
                    "Real decoded audio drives the playback level");
            player.setVolume(65, true);
            require(waitFor([&] { return player.snapshot().level == 0; }, 2s),
                    "Muting clears the playback level");
            player.setVolume(65, false);
            server.fail = true;
            require(waitFor([&] { return player.snapshot().state == PlaybackState::Reconnecting; }, 15s),
                    "Stream loss triggers reconnect");
            server.fail = false;
            require(waitFor(
                        [&] {
                            return player.snapshot().state == PlaybackState::Playing &&
                                   server.connections >= 2;
                        },
                        8s),
                    "Playback recovers after server returns");
            player.stop();
            require(player.snapshot().state == PlaybackState::Stopped && player.snapshot().level == 0,
                    "Stop clears playback and its level immediately");
            Station bad = a;
            bad.id = newId();
            bad.url = "http://127.0.0.1:1/unreachable";
            for (int i = 0; i < 20; ++i) {
                player.play(bad);
                player.play(a);
            }
            require(waitFor(
                        [&] {
                            auto s = player.snapshot();
                            return s.state == PlaybackState::Playing && s.stationId == a.id;
                        },
                        8s),
                    "Rapid switching keeps latest station");
            player.stop();
            std::this_thread::sleep_for(500ms);
            require(player.snapshot().state == PlaybackState::Stopped,
                    "Stop prevents stale connection revival");
        }
        require(fs::file_size(diagnostics) < 64 * 1024, "Local diagnostics stay bounded");
        const auto log = readFile(diagnostics);
        require(log.find("attempt-started") != std::string::npos &&
                    log.find("do-not-log-this") == std::string::npos &&
                    log.find("http:") == std::string::npos && log.find("Test Artist") == std::string::npos,
                "Diagnostics omit stream URLs, tokens and song metadata");
        fs::remove(diagnostics);
        {
            Server live;
            Station good{newId(), "Healthy", "http://127.0.0.1:" + std::to_string(live.port) + "/live",
                         "",      "",        true};
            AudioPlayer player(true);
            for (auto mode :
                 {Server::Mode::Headers, Server::Mode::Audio, Server::Mode::Dribble, Server::Mode::Hls}) {
                Server held(mode);
                Station slow{newId(), "Held", "http://127.0.0.1:" + std::to_string(held.port) + "/live",
                             "",      "",     true};
                player.play(slow);
                require(waitFor([&] { return held.connections >= (mode == Server::Mode::Hls ? 2 : 1); }, 4s),
                        "Decoder reaches withheld headers/audio/dribble/HLS fixture");
                const int closed = held.closed;
                player.play(good);
                require(waitFor(
                            [&] {
                                return player.snapshot().state == PlaybackState::Playing &&
                                       player.snapshot().stationId == good.id;
                            },
                            4s),
                        "Healthy station replaces pending decoder within four seconds");
                require(waitFor([&] { return held.closed > closed; }, 2s),
                        "Superseded decoder really closes its socket");
                player.play(slow);
                require(waitFor([&] { return held.connections >= (mode == Server::Mode::Hls ? 4 : 2); }, 4s),
                        "Second pending attempt reaches server");
                const int before = held.closed;
                player.stop();
                require(waitFor([&] { return held.closed > before; }, 2s),
                        "Stop closes pending network operation promptly");
                require(player.snapshot().state == PlaybackState::Stopped,
                        "Late completion cannot revive stopped playback");
            }
            {
                Server drip(Server::Mode::Dribble);
                Station slow{newId(), "Drip", "http://127.0.0.1:" + std::to_string(drip.port) + "/live",
                             "",      "",     true};
                player.play(slow);
                require(waitFor([&] { return drip.connections >= 1; }, 4s),
                        "Deadline fixture accepted connection");
                require(waitFor(
                            [&] {
                                return player.snapshot().state == PlaybackState::Reconnecting &&
                                       drip.closed > 0;
                            },
                            17s),
                        "Total startup deadline retires a dribbling decoder and schedules retry");
                player.stop();
            }
            player.play(good);
            require(waitFor([&] { return player.snapshot().state == PlaybackState::Playing; }, 4s),
                    "Playback works after deadline recovery");
            const int previous = live.connections;
            player.reset();
            require(waitFor(
                        [&] {
                            return player.snapshot().state == PlaybackState::Playing &&
                                   live.connections > previous;
                        },
                        4s),
                    "Internal reset creates a fresh working decoder");
            HANDLE frozen = decoderProcess(PROCESS_QUERY_LIMITED_INFORMATION);
            require(frozen != nullptr, "Watchdog fixture identifies its decoder process");
            const DWORD frozenPid = GetProcessId(frozen);
            CloseHandle(frozen);
            HANDLE threads = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
            THREADENTRY32 entry{sizeof(entry)};
            unsigned suspended = 0;
            if (threads != INVALID_HANDLE_VALUE && Thread32First(threads, &entry))
                do {
                    if (entry.th32OwnerProcessID == frozenPid) {
                        HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME, FALSE, entry.th32ThreadID);
                        if (thread) {
                            if (SuspendThread(thread) != static_cast<DWORD>(-1))
                                ++suspended;
                            CloseHandle(thread);
                        }
                    }
                } while (Thread32Next(threads, &entry));
            if (threads != INVALID_HANDLE_VALUE)
                CloseHandle(threads);
            require(suspended > 0, "Test deliberately freezes only its own decoder threads");
            const int frozenConnections = live.connections;
            require(waitFor(
                        [&] {
                            return player.snapshot().state == PlaybackState::Playing &&
                                   live.connections > frozenConnections;
                        },
                        10s),
                    "Progress watchdog replaces an unresponsive decoder safely");
            HANDLE child = decoderProcess(PROCESS_TERMINATE | SYNCHRONIZE);
            require(child != nullptr, "Crash fixture identifies its own audio process");
            TerminateProcess(child, 99);
            CloseHandle(child);
            const int crashed = live.connections;
            require(waitFor(
                        [&] {
                            return player.snapshot().state == PlaybackState::Playing &&
                                   live.connections > crashed;
                        },
                        6s),
                    "Unexpected decoder crash recovers automatically");
            require(runningDecoders() == 1, "Recovery retains exactly one live decoder");
            player.stop();
            require(waitFor([&] { return runningDecoders() == 0; }, 2s), "Stop retires the live helper");
            live.fail = true;
            const int baseline = live.connections;
            player.play(good);
            require(waitFor(
                        [&] {
                            return live.connections >= baseline + 3 &&
                                   player.snapshot().state == PlaybackState::Reconnecting;
                        },
                        8s),
                    "Several consecutive failures remain in automatic retry");
            live.fail = false;
            require(waitFor([&] { return player.snapshot().state == PlaybackState::Playing; }, 8s),
                    "Repeated failure recovers automatically when the server returns");
            live.badContent = true;
            player.play(good);
            require(waitFor([&] { return player.snapshot().state == PlaybackState::Reconnecting; }, 6s),
                    "Temporary non-audio response is retried automatically");
            live.badContent = false;
            require(waitFor([&] { return player.snapshot().state == PlaybackState::Playing; }, 8s),
                    "Format probing failure recovers without manual engine reset");
            live.badContent = true;
            player.play(good);
            require(waitFor([&] {
                        return player.snapshot().detail.find("Automatic probe in 120s") !=
                               std::string::npos;
                    }, 14s),
                    "Persistent non-audio format responses enter a bounded automatic cooldown");
            const int cooledConnections = live.connections;
            std::this_thread::sleep_for(1200ms);
            require(live.connections == cooledConnections && runningDecoders() == 0,
                    "Cooldown prevents rapid decoder restart loops");
            player.stop();
            require(player.snapshot().state == PlaybackState::Stopped && runningDecoders() == 0,
                    "Stop cancels a persistent-format cooldown immediately");
            Server alternate;
            Station other = good;
            other.id = newId();
            other.url = "http://127.0.0.1:" + std::to_string(alternate.port) + "/live";
            player.play(other);
            require(waitFor([&] { return player.snapshot().state == PlaybackState::Playing; }, 4s),
                    "Station switching bypasses the previous station's cooldown");
            live.badContent = false;
            player.play(good);
            require(waitFor([&] { return player.snapshot().state == PlaybackState::Playing; }, 4s),
                    "Fresh Play retries immediately after a persistent format error");
            live.fail = true;
            player.play(good);
            require(waitFor([&] { return player.snapshot().state == PlaybackState::Reconnecting; }, 4s),
                    "Retry can be stopped during backoff");
            const int beforeStop = live.connections;
            player.stop();
            std::this_thread::sleep_for(1500ms);
            require(live.connections == beforeStop && runningDecoders() == 0,
                    "Stop cancels scheduled retries and leaves no helper");
        }
        {
            Server held(Server::Mode::Headers);
            const auto begin = std::chrono::steady_clock::now();
            {
                AudioPlayer player(true);
                player.play({newId(), "Shutdown", "http://127.0.0.1:" + std::to_string(held.port) + "/live",
                             "", "", true});
                require(waitFor([&] { return held.connections > 0; }, 4s),
                        "Shutdown fixture enters decoder startup");
            }
            require(std::chrono::steady_clock::now() - begin < 5s &&
                        waitFor([&] { return held.closed > 0; }, 1s),
                    "Shutdown joins supervisor and closes blocked decoder within deadline");
        }
        {
            wchar_t executable[32768]{};
            GetModuleFileNameW(nullptr, executable, 32768);
            const auto source = fs::path(executable).parent_path();
            const auto destination = fs::current_path() / "test-data" / ("audio-launch-" + newId());
            fs::create_directories(destination);
            const auto childExe = destination / L"audio_tests.exe";
            fs::copy_file(executable, childExe);
            auto command = L"\"" + childExe.wstring() + L"\" --launch-retry \"" + source.wstring() + L"\"";
            STARTUPINFOW startup{sizeof(startup)};
            PROCESS_INFORMATION child{};
            require(CreateProcessW(childExe.c_str(), command.data(), nullptr, nullptr, FALSE,
                                   CREATE_NO_WINDOW, nullptr, nullptr, &startup, &child) != FALSE,
                    "Isolated missing-helper recovery fixture starts");
            const auto wait = WaitForSingleObject(child.hProcess, 15000);
            if (wait != WAIT_OBJECT_0) {
                TerminateProcess(child.hProcess, 99);
                WaitForSingleObject(child.hProcess, 1000);
            }
            DWORD exit = 1;
            GetExitCodeProcess(child.hProcess, &exit);
            CloseHandle(child.hThread);
            CloseHandle(child.hProcess);
            std::error_code ec;
            for (const auto* name :
                 {L"audio_tests.exe", L"QuickStationAudio.exe", L"bass.dll", L"basshls.dll"})
                fs::remove(destination / name, ec);
            fs::remove(destination, ec);
            require(wait == WAIT_OBJECT_0 && exit == 0,
                    "Transient helper launch failure recovers automatically once files are available");
        }
        for (bool invalid : {false, true}) {
            wchar_t executable[32768]{};
            GetModuleFileNameW(nullptr, executable, 32768);
            const auto source = fs::path(executable).parent_path();
            const auto destination = fs::current_path() / "test-data" / ("audio-plugin-" + newId());
            fs::create_directories(destination);
            for (const auto* name : {L"audio_tests.exe", L"QuickStationAudio.exe", L"bass.dll"})
                fs::copy_file(source / name, destination / name);
            if (invalid)
                std::ofstream(destination / "basshls.dll") << "invalid test dependency";
            auto childExe = destination / L"audio_tests.exe";
            auto command = L"\"" + childExe.wstring() + L"\" --plugin-check";
            STARTUPINFOW startup{sizeof(startup)};
            PROCESS_INFORMATION child{};
            require(CreateProcessW(childExe.c_str(), command.data(), nullptr, nullptr, FALSE,
                                   CREATE_NO_WINDOW, nullptr, destination.c_str(), &startup, &child) != FALSE,
                    "Isolated plugin-failure fixture starts");
            const auto wait = WaitForSingleObject(child.hProcess, 15000);
            if (wait != WAIT_OBJECT_0) {
                TerminateProcess(child.hProcess, 99);
                WaitForSingleObject(child.hProcess, 1000);
            }
            DWORD exit = 1;
            GetExitCodeProcess(child.hProcess, &exit);
            CloseHandle(child.hThread);
            CloseHandle(child.hProcess);
            std::error_code ignored;
            for (const auto* name : {L"audio_tests.exe", L"QuickStationAudio.exe", L"bass.dll",
                                     L"basshls.dll", L"plugin-test.log"})
                fs::remove(destination / name, ignored);
            fs::remove(destination, ignored);
            require(wait == WAIT_OBJECT_0 && exit == 0,
                    invalid ? "Invalid HLS plugin is diagnosed without blocking ordinary playback"
                            : "Missing HLS plugin is diagnosed without blocking ordinary playback");
        }
        std::cout << checks << " audio checks passed\n";
        WSACleanup();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        WSACleanup();
        return 1;
    }
}
