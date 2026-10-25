#include "core.h"
#include <functional>
#include <iostream>
#include <json.hpp>
#include <stdexcept>
#include <windows.h>

using namespace radio;
static int checks = 0;
static void check(bool condition, const char* name) {
    ++checks;
    if (!condition)
        throw std::runtime_error(name);
}
template <class Fn> void rejects(Fn fn, const char* name) {
    bool threw = false;
    try {
        fn();
    } catch (const std::exception&) {
        threw = true;
    }
    check(threw, name);
}
int main() {
    auto root = fs::current_path() / "test-data" / ("radio-tests-" + newId());
    try {
        check(validUrl("https://example.org/live?token=abc"), "HTTPS accepted");
        check(validUrl("http://127.0.0.1:8000/live"), "Local radio accepted");
        for (const char* s :
             {"", "file:///C:/x", "javascript:alert(1)", "https://", "https://:80", "https://x\r\nX-Evil:yes",
              "https://user:pass@host/live", "https://has space/live"})
            check(!validUrl(s), "Unsafe or malformed URL rejected");
        Library lib;
        lib.stations = {{newId(), "موسيقى · Jazz 🎵", "https://example.org/live",
                         "https://example.org/logo.png", "Jazz", true}};
        lib.volume = 42;
        lib.muted = true;
        lib.doubleClick = true;
        lib.lastStation = lib.stations[0].id;
        auto copy = decode(encode(lib));
        check(copy.stations[0].name == lib.stations[0].name, "Unicode roundtrip");
        check(copy.volume == 42 && copy.muted && copy.doubleClick, "Settings roundtrip");
        check(copy.lastStation == lib.lastStation, "Last station roundtrip");
        check(utf8(wide(lib.stations[0].name)) == lib.stations[0].name, "UTF conversion");
        auto j = nlohmann::json::parse(encode(lib));
        j["settings"]["volume"] = 400;
        check(decode(j.dump()).volume == 100, "Volume clamped");
        j["stations"].push_back(j["stations"][0]);
        check(decode(j.dump()).stations.size() == 1, "Duplicate URL deduplicated");
        j["stations"][1]["url"] = "https://other.org/live";
        auto ids = decode(j.dump());
        check(ids.stations[0].id != ids.stations[1].id, "Duplicate ID repaired");
        rejects([] { decode("{broken}"); }, "Malformed JSON rejected");
        rejects([] { decode(R"({"version":2,"stations":[]})"); }, "Future version rejected");
        rejects([] { decode(R"({"version":1,"stations":[{"name":true,"url":"https://x"}]})"); },
                "Wrong field type rejected");
        rejects([] { decode(std::string(1000, '[') + std::string(1000, ']')); }, "Deep nesting rejected");
        rejects([] { decode(std::string(MaxDocumentBytes + 1, ' ')); }, "Oversized file rejected");
        auto m3u = writePlaylist(lib.stations);
        auto imported = parsePlaylist(m3u);
        check(imported.size() == 1 && imported[0].name == lib.stations[0].name &&
                  imported[0].logo == lib.stations[0].logo,
              "M3U roundtrip");
        check(parsePlaylist("\xef\xbb\xbf#EXTM3U\r\n#EXTINF:-1,Hello\r\nhttps://x/live\r\n")[0].name ==
                  "Hello",
              "BOM and CRLF M3U");
        rejects([] { parsePlaylist("#EXTM3U\nfile:///private"); }, "Local file import rejected");
        rejects([] { parsePlaylist("#EXTM3U\n"); }, "Empty M3U rejected");
        auto before = encode(lib);
        check(mergeStations(lib, lib.stations) == 0, "Merge duplicate skipped");
        check(encode(lib) == before, "Merge preserves settings");
        auto bad = lib.stations[0];
        bad.url = "file:///oops";
        rejects([&] { mergeStations(lib, {bad}); }, "Invalid merge rejected");
        check(encode(lib) == before, "Merge failure is atomic");
        check(metadataTitle("StreamTitle='Artist - Song';StreamUrl='';") == "Artist - Song", "ICY title");
        check(metadataTitle("StreamTitle='Don't stop';") == "Don't stop", "Apostrophes in title");
        check(metadataTitle("StreamTitle='partial").empty(), "Partial metadata ignored");
        check(retryDelay(0).count() == 1 && retryDelay(3).count() == 8 && retryDelay(500).count() == 32,
              "Bounded backoff");
        check(cacheKey("https://x") == cacheKey("https://x") &&
                  cacheKey("https://x") != cacheKey("https://y"),
              "Stable cache key");
        fs::create_directories(root);
        Store store(root);
        store.save(lib);
        check(store.load().stations[0].name == lib.stations[0].name, "Disk roundtrip");
        auto newer = lib;
        newer.volume = 23;
        store.save(newer);
        check(decode(readFile(root / "stations.json.bak")).volume == 42, "Previous version backed up");
        auto oversized = lib;
        oversized.stations.clear();
        for (int i = 0; i < 2100; ++i) {
            auto station = lib.stations.front();
            station.id = newId();
            station.url = "https://example.org/" + std::to_string(i) + "/" + std::string(4000, 'x');
            oversized.stations.push_back(std::move(station));
        }
        rejects([&] { store.save(oversized); }, "Cannot save a library larger than the reader accepts");
        check(store.load().volume == 23 && decode(readFile(root / "stations.json.bak")).volume == 42,
              "Oversized save preserves both the current library and its recovery backup");
        rejects([&] { writePlaylist(oversized.stations); }, "Export respects the import size limit");
        atomicWrite(root / "stations.json", "damaged", false);
        auto recovered = store.load();
        check(recovered.volume == 42 && !store.warning().empty(), "Corrupt file recovered");
        bool preserved = false;
        for (auto& p : fs::directory_iterator(root))
            if (p.path().filename().wstring().starts_with(L"stations.corrupt-"))
                preserved = true;
        check(preserved, "Corruption preserved");
        auto blocked = root / "locked.json";
        atomicWrite(blocked, "original", false);
        HANDLE h = CreateFileW(blocked.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        rejects([&] { atomicWrite(blocked, "replacement"); }, "Locked destination reports failure");
        CloseHandle(h);
        check(readFile(blocked) == "original", "Failed save keeps old content");
        auto orphan = root / "orphan";
        fs::create_directories(orphan);
        atomicWrite(orphan / "stations.json", "broken", false);
        Store broken(orphan);
        rejects([&] { broken.load(); }, "Corruption without backup is not reset");
        check(readFile(orphan / "stations.json") == "broken", "Unrecoverable data preserved");
        rejects([&] { readFile(root / "stations.json", 2); }, "Read size bounded");
        fs::remove_all(root);
        std::cout << "PASS: " << checks << " core checks\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << "\nTest files: " << root << '\n';
        return 1;
    }
}
