#include "core.h"
#include <iomanip>
#include <json.hpp>
#include <objbase.h>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <windows.h>

namespace radio {
using json = nlohmann::json;
std::wstring wide(std::string_view s) {
    if (s.empty())
        return {};
    int n =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (!n) { // ICY servers also use legacy Windows text encodings.
        n = MultiByteToWideChar(CP_ACP, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
        std::wstring out(n, L'\0');
        MultiByteToWideChar(CP_ACP, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
        return out;
    }
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}
std::string utf8(std::wstring_view s) {
    if (s.empty())
        return {};
    int n =
        WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n, nullptr, nullptr);
    return out;
}
std::string trim(std::string s) {
    auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos)
        return {};
    return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}
bool validUrl(std::string_view s) {
    if (s.empty() || s.size() > 4096)
        return false;
    for (unsigned char c : s)
        if (c <= 32 || c == 127 || c == '\\')
            return false;
    size_t start = s.starts_with("https://") ? 8 : s.starts_with("http://") ? 7 : 0;
    if (!start)
        return false;
    auto end = s.find_first_of("/?#", start);
    auto authority = s.substr(start, end == s.npos ? s.size() - start : end - start);
    return !authority.empty() && authority.find('@') == authority.npos && authority.front() != ':';
}
static bool cleanText(std::string_view s, size_t limit) {
    if (s.size() > limit)
        return false;
    return std::none_of(s.begin(), s.end(), [](unsigned char c) { return c < 32 || c == 127; });
}
void validate(const Station& s) {
    if (trim(s.name).empty() || !cleanText(s.name, 256))
        throw std::runtime_error("Station name must contain 1–256 characters and no control characters.");
    if (!validUrl(s.url))
        throw std::runtime_error(
            "Enter a valid HTTP or HTTPS stream URL, without spaces or embedded credentials.");
    if (!s.logo.empty() && !validUrl(s.logo))
        throw std::runtime_error("Logo URL must use HTTP or HTTPS.");
    if (!cleanText(s.genre, 256) || !cleanText(s.id, 128))
        throw std::runtime_error("Invalid station details.");
}
std::string newId() {
    GUID id{};
    if (FAILED(CoCreateGuid(&id)))
        throw std::runtime_error("Could not create station ID.");
    wchar_t text[40]{};
    StringFromGUID2(id, text, 40);
    return utf8(text);
}
static json stationJson(const Station& s) {
    validate(s);
    return {{"id", s.id},     {"name", s.name},   {"url", s.url},
            {"logo", s.logo}, {"genre", s.genre}, {"favorite", s.favorite}};
}
std::string encode(const Library& lib) {
    if (lib.stations.size() > MaxStations)
        throw std::runtime_error("Too many stations.");
    json stations = json::array();
    for (const auto& s : lib.stations)
        stations.push_back(stationJson(s));
    auto document = json{{"version", 1},
                         {"stations", stations},
                         {"settings",
                          {{"volume", std::clamp(lib.volume, 0, 100)},
                           {"muted", lib.muted},
                           {"doubleClick", lib.doubleClick},
                           {"lastStation", lib.lastStation}}}}
                        .dump(2);
    if (document.size() > MaxDocumentBytes)
        throw std::runtime_error("Station list exceeds the 8 MB file limit. Nothing was saved.");
    return document;
}
Library decode(std::string_view doc) {
    if (doc.size() > MaxDocumentBytes)
        throw std::runtime_error("Station file is too large (8 MB maximum).");
    // Stop deeply nested input before the recursive JSON parser can exhaust the stack.
    auto j = json::parse(doc, [](int depth, json::parse_event_t, json&) {
        if (depth > 32)
            throw std::runtime_error("JSON nesting is too deep.");
        return true;
    });
    if (!j.is_object() || j.value("version", 0) != 1 || !j.contains("stations") || !j["stations"].is_array())
        throw std::runtime_error("Unsupported station file. Expected QuickStation JSON version 1.");
    if (j["stations"].size() > MaxStations)
        throw std::runtime_error("Station limit is 5,000.");
    Library lib;
    std::unordered_set<std::string> ids, urls;
    for (const auto& v : j["stations"]) {
        Station s{v.value("id", ""),
                  v.at("name").get<std::string>(),
                  v.at("url").get<std::string>(),
                  v.value("logo", ""),
                  v.value("genre", ""),
                  v.value("favorite", true)};
        validate(s);
        if (s.id.empty() || ids.contains(s.id))
            s.id = newId();
        ids.insert(s.id);
        if (urls.insert(s.url).second)
            lib.stations.push_back(std::move(s));
    }
    if (j.contains("settings")) {
        const auto& v = j["settings"];
        lib.volume = std::clamp(v.value("volume", 65), 0, 100);
        lib.muted = v.value("muted", false);
        lib.doubleClick = v.value("doubleClick", false);
        lib.lastStation = v.value("lastStation", "");
        if (lib.lastStation.size() > 128)
            lib.lastStation.clear();
    }
    return lib;
}
std::vector<Station> parsePlaylist(std::string_view doc) {
    if (doc.size() > MaxDocumentBytes)
        throw std::runtime_error("Playlist is too large.");
    if (doc.starts_with("\xef\xbb\xbf"))
        doc.remove_prefix(3);
    auto text = trim(std::string(doc));
    if (text.starts_with("{"))
        return decode(text).stations;
    std::istringstream lines(text);
    std::string line, title, logo;
    std::vector<Station> result;
    while (std::getline(lines, line)) {
        line = trim(line);
        if (line.empty())
            continue;
        if (line.starts_with("#EXTINF:")) {
            auto pos = line.find(',');
            title = pos == line.npos ? "" : trim(line.substr(pos + 1));
        } else if (line.starts_with("#EXTIMG:"))
            logo = trim(line.substr(8));
        else if (line.front() != '#') {
            Station s{newId(), title.empty() ? line : title, line, logo, "", true};
            validate(s);
            result.push_back(std::move(s));
            title.clear();
            logo.clear();
            if (result.size() > MaxStations)
                throw std::runtime_error("Station limit is 5,000.");
        }
    }
    if (result.empty())
        throw std::runtime_error(
            "No stations found. Import QuickStation JSON or an M3U playlist with HTTP(S) URLs.");
    return result;
}
std::string writePlaylist(const std::vector<Station>& stations) {
    if (stations.size() > MaxStations)
        throw std::runtime_error("Too many stations.");
    std::string out = "#EXTM3U\n";
    for (const auto& s : stations) {
        validate(s);
        out += "#EXTINF:-1," + s.name + "\n";
        if (!s.logo.empty())
            out += "#EXTIMG:" + s.logo + "\n";
        out += s.url + "\n";
        if (out.size() > MaxDocumentBytes)
            throw std::runtime_error("Playlist exceeds the 8 MB file limit. Nothing was exported.");
    }
    return out;
}
size_t mergeStations(Library& lib, const std::vector<Station>& stations) {
    Library copy = lib;
    std::unordered_set<std::string> urls;
    for (const auto& s : copy.stations)
        urls.insert(s.url);
    size_t added = 0;
    for (auto s : stations) {
        validate(s);
        if (urls.insert(s.url).second) {
            s.id = newId();
            copy.stations.push_back(std::move(s));
            ++added;
        }
    }
    if (copy.stations.size() > MaxStations)
        throw std::runtime_error("Station limit is 5,000. Nothing was imported.");
    lib = std::move(copy);
    return added;
}
std::string metadataTitle(std::string_view meta) {
    auto start = meta.find("StreamTitle='");
    if (start == meta.npos)
        return {};
    start += 13;
    auto end = meta.find("';", start);
    if (end == meta.npos)
        return {};
    auto title = std::string(meta.substr(start, std::min<size_t>(end - start, 1024)));
    for (auto& c : title)
        if (static_cast<unsigned char>(c) < 32)
            c = ' ';
    return trim(title);
}
std::chrono::seconds retryDelay(unsigned failures) {
    return std::chrono::seconds(1u << std::min(failures, 5u));
}
std::string cacheKey(std::string_view url) {
    uint64_t hash = 14695981039346656037ull;
    for (unsigned char c : url) {
        hash ^= c;
        hash *= 1099511628211ull;
    }
    std::ostringstream out;
    out << std::hex << std::setw(16) << std::setfill('0') << hash;
    return out.str();
}
Library starterLibrary() {
    Library lib;
    lib.stations = {{newId(), "SomaFM · Groove Salad", "https://ice2.somafm.com/groovesalad-128-mp3",
                     "https://somafm.com/img3/groovesalad-400.jpg", "Ambient · Downtempo", true},
                    {newId(), "SomaFM · Drone Zone", "https://ice2.somafm.com/dronezone-128-mp3",
                     "https://somafm.com/img3/dronezone-400.jpg", "Atmospheric · Ambient", true},
                    {newId(), "SomaFM · Secret Agent", "https://ice2.somafm.com/secretagent-128-mp3",
                     "https://somafm.com/img3/secretagent-400.jpg", "Lounge · Cinematic", true},
                    {newId(), "SomaFM · DEF CON", "https://ice2.somafm.com/defcon-128-mp3",
                     "https://somafm.com/img3/defcon-400.jpg", "Electronic · Underground", true}};
    return lib;
}
} // namespace radio
