#include "audio_ipc.h"
#include "core.h"
#include <bass.h>
#include <objbase.h>
#include <shellapi.h>
#include <algorithm>
#include <cstring>

namespace radio {
namespace {
bool permanentError(int code) {
    // A station can temporarily return non-audio content or fail codec probing. Retry those.
    return code == BASS_ERROR_ILLPARAM || code == BASS_ERROR_PROTOCOL || code == BASS_ERROR_DENIED;
}
std::string titleFor(DWORD stream) {
    if (const char* tags = BASS_ChannelGetTags(stream, BASS_TAG_META)) {
        auto title = metadataTitle(std::string_view(tags, strnlen_s(tags, 8192)));
        if (!title.empty())
            return title;
    }
    if (const char* tags = BASS_ChannelGetTags(stream, BASS_TAG_OGG)) {
        std::string title, artist;
        size_t count = 0;
        while (*tags && count < 65536) {
            size_t length = strnlen_s(tags, 4096);
            if (length == 4096)
                break;
            std::string_view tag(tags, length);
            if (tag.starts_with("TITLE="))
                title = tag.substr(6);
            if (tag.starts_with("ARTIST="))
                artist = tag.substr(7);
            tags += length + 1;
            count += length + 1;
        }
        if (!title.empty())
            return artist.empty() ? title : artist + " — " + title;
    }
    return {};
}

void publish(ipc::Shared& shared, HANDLE mutex, ipc::State state, int error,
             const std::string& title = {}, int pluginError = 0, uint16_t level = 0) {
    const auto result = WaitForSingleObject(mutex, 1000);
    if (result != WAIT_OBJECT_0 && result != WAIT_ABANDONED)
        return;
    shared.report.heartbeat = GetTickCount64();
    shared.report.state = state;
    shared.report.error = error;
    shared.report.pluginError = pluginError;
    shared.report.level = state == ipc::State::Playing ? level : 0;
    strncpy_s(shared.report.title, title.c_str(), _TRUNCATE);
    ReleaseMutex(mutex);
}
int play(ipc::Shared& shared, HANDLE stop, HANDLE mutex) {
    BASS_SetConfig(BASS_CONFIG_DEV_DEFAULT, 1);
    BASS_SetConfig(BASS_CONFIG_NET_TIMEOUT, 7000);
    BASS_SetConfig(BASS_CONFIG_NET_READTIMEOUT, 7000);
    BASS_SetConfig(BASS_CONFIG_NET_BUFFER, 4000);
    BASS_SetConfig(BASS_CONFIG_NET_PREBUF, 20);
    BASS_SetConfig(BASS_CONFIG_NET_PLAYLIST, 1);
    BASS_SetConfig(BASS_CONFIG_NET_PLAYLIST_DEPTH, 3);
    BASS_SetConfigPtr(BASS_CONFIG_NET_AGENT, "QuickStation/1.6.14");
    DWORD stream = 0;
    HPLUGIN plugin = 0;
    bool initialized = BASS_Init(shared.silent ? 0 : shared.device, 44100, 0, nullptr, nullptr) != FALSE;
    int error = BASS_ErrorGetCode();
    int pluginError = 0;
    if (initialized) {
        wchar_t exe[32768]{};
        GetModuleFileNameW(nullptr, exe, 32768);
        auto path = fs::path(exe).parent_path() / L"basshls.dll";
        plugin = BASS_PluginLoad(reinterpret_cast<const char*>(path.c_str()), BASS_UNICODE);
        if (!plugin)
            pluginError = BASS_ErrorGetCode(); // Capture before StreamCreateURL overwrites BASS's error.
        stream =
            BASS_StreamCreateURL(reinterpret_cast<const char*>(shared.url), 0,
                                 BASS_UNICODE | BASS_STREAM_BLOCK | BASS_STREAM_RESTRATE, nullptr, &shared);
        error = BASS_ErrorGetCode();
    }
    if (stream) {
        BASS_ChannelSetAttribute(stream, BASS_ATTRIB_VOL,
                                 InterlockedCompareExchange(&shared.volume, 0, 0) / 100.0f);
        if (!BASS_ChannelPlay(stream, FALSE)) {
            error = BASS_ErrorGetCode();
            publish(shared, mutex, ipc::State::Retry, error, {}, pluginError);
        } else {
            while (WaitForSingleObject(stop, 100) == WAIT_TIMEOUT) {
                const int volume = InterlockedCompareExchange(&shared.volume, 0, 0);
                BASS_ChannelSetAttribute(stream, BASS_ATTRIB_VOL, volume / 100.0f);
                const auto active = BASS_ChannelIsActive(stream);
                if (active == BASS_ACTIVE_STOPPED) {
                    publish(shared, mutex, ipc::State::Retry, 0, {}, pluginError);
                    break;
                }
                uint16_t level = 0;
                if (active == BASS_ACTIVE_PLAYING && volume > 0) {
                    const DWORD peaks = BASS_ChannelGetLevel(stream);
                    if (peaks != 0xffffffffUL) {
                        const int peak = std::max<int>(LOWORD(peaks), HIWORD(peaks));
                        level = static_cast<uint16_t>(std::min(32768, peak * volume / 100));
                    }
                }
                publish(shared, mutex,
                        active == BASS_ACTIVE_PLAYING ? ipc::State::Playing : ipc::State::Buffering, 0,
                        titleFor(stream), pluginError, level);
            }
        }
    } else {
        publish(shared, mutex, permanentError(error) ? ipc::State::Error : ipc::State::Retry, error,
                {}, pluginError);
    }
    // A driver/decoder can also block during cleanup. The parent bounds this process's lifetime.
    if (stream)
        BASS_StreamFree(stream);
    if (plugin)
        BASS_PluginFree(plugin);
    if (initialized)
        BASS_Free();
    return 0;
}
} // namespace
} // namespace radio
int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    int count = 0;
    auto args = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!args)
        return 2;
    HANDLE handles[3]{};
    bool valid = count == 4;
    if (valid)
        for (int i = 0; i < 3; ++i) {
            wchar_t* end = nullptr;
            const auto value = wcstoull(args[i + 1], &end, 10);
            valid = valid && value != 0 && end && *end == 0;
            handles[i] = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(value));
        }
    LocalFree(args);
    if (!valid)
        return 2;
    radio::ipc::Handle mapping(handles[0]), stop(handles[1]), mutex(handles[2]);
    auto* shared = static_cast<radio::ipc::Shared*>(
        MapViewOfFile(mapping.value, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(radio::ipc::Shared)));
    if (!shared)
        return 2;
    int result = 2;
    if (shared->version == radio::ipc::Version && shared->url[4096] == 0) {
        const auto com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        try {
            result = radio::play(*shared, stop.value, mutex.value);
        } catch (...) {
            result = 3;
        }
        if (SUCCEEDED(com))
            CoUninitialize();
    }
    UnmapViewOfFile(shared);
    return result;
}
