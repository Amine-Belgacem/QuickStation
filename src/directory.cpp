#include "directory.h"
#include <windows.h>
#include <json.hpp>
#include <set>
#include <stdexcept>

namespace radio {
namespace {
nlohmann::json parse(std::string_view document) {
    if (document.size() > 2 * 1024 * 1024)
        throw std::runtime_error("Directory response is too large.");
    auto data =
        nlohmann::json::parse(document, [](int depth, nlohmann::json::parse_event_t, nlohmann::json&) {
            if (depth > 24)
                throw std::runtime_error("Directory response is too deeply nested.");
            return true;
        });
    if (!data.is_array())
        throw std::runtime_error("Invalid directory response.");
    return data;
}
std::string text(const nlohmann::json& value, const char* key, size_t limit = 256) {
    auto result = trim(value.value(key, std::string{}));
    if (result.size() > limit)
        throw std::runtime_error("Directory text is too long.");
    return result;
}
std::string encodeParameter(std::string_view value) {
    constexpr char hex[] = "0123456789ABCDEF";
    std::string encoded;
    for (unsigned char c : value) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
            c == '_' || c == '.' || c == '~')
            encoded += static_cast<char>(c);
        else {
            encoded += '%';
            encoded += hex[c >> 4];
            encoded += hex[c & 15];
        }
    }
    return encoded;
}
bool countryCode(std::string_view value) {
    return value.size() == 2 && value[0] >= 'A' && value[0] <= 'Z' && value[1] >= 'A' && value[1] <= 'Z';
}
std::string countryName(const std::string& code, std::string fallback) {
    if (!countryCode(code))
        return fallback;
    auto location = wide(code);
    wchar_t name[128]{};
    return GetGeoInfoEx(location.data(), GEO_FRIENDLYNAME, name, 128) > 0 ? utf8(name) : fallback;
}
} // namespace
std::string browsePath(const BrowseQuery& query) {
    if (query.name.size() > 256 || query.genre.size() > 256 ||
        (!query.countryCode.empty() && !countryCode(query.countryCode)))
        throw std::runtime_error("Invalid station filters.");
    std::string path = "/json/stations/search?hidebroken=true&limit=80&order=votes&reverse=true";
    if (!query.name.empty())
        path += "&name=" + encodeParameter(query.name);
    if (!query.countryCode.empty())
        path += "&countrycode=" + query.countryCode;
    if (!query.genre.empty())
        path += "&tag=" + encodeParameter(query.genre) + "&tagExact=true";
    return path;
}
std::vector<DirectoryStation> parseDirectoryStations(std::string_view document) {
    std::vector<DirectoryStation> stations;
    std::set<std::string> urls;
    for (const auto& value : parse(document)) {
        if (stations.size() >= DirectoryLimit)
            break;
        try {
            Station station{newId(),
                            text(value, "name"),
                            text(value, "url_resolved", 4096),
                            text(value, "favicon", 4096),
                            text(value, "tags"),
                            true};
            if (station.url.empty())
                station.url = text(value, "url", 4096);
            if (!validUrl(station.logo))
                station.logo.clear();
            validate(station);
            auto country = countryName(text(value, "countrycode"), text(value, "country"));
            if (urls.insert(station.url).second)
                stations.push_back({std::move(station), std::move(country)});
        } catch (const std::exception&) { /* One malformed station cannot discard valid entries. */
        }
    }
    return stations;
}
DirectoryFilters parseDirectoryFilters(std::string_view countries, std::string_view genres) {
    DirectoryFilters filters;
    std::set<std::string> codes, tags;
    for (const auto& value : parse(countries)) {
        if (filters.countries.size() >= 300)
            break;
        try {
            auto code = text(value, "name");
            if (!countryCode(code) || !codes.insert(code).second)
                continue;
            auto label = countryName(code, code);
            filters.countries.push_back({std::move(code), std::move(label)});
        } catch (const std::exception&) {
        }
    }
    for (const auto& value : parse(genres)) {
        if (filters.genres.size() >= 200)
            break;
        try {
            auto tag = text(value, "name");
            if (!tag.empty() && tags.insert(tag).second)
                filters.genres.push_back(std::move(tag));
        } catch (const std::exception&) {
        }
    }
    std::sort(filters.countries.begin(), filters.countries.end(),
              [](const Country& a, const Country& b) { return a.name < b.name; });
    std::sort(filters.genres.begin(), filters.genres.end());
    return filters;
}
} // namespace radio
