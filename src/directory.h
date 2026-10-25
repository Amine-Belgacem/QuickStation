#pragma once
#include "core.h"

namespace radio {
struct BrowseQuery {
    std::string name, countryCode, genre;
};
struct DirectoryStation {
    Station station;
    std::string country;
};
struct Country {
    std::string code, name;
};
struct DirectoryFilters {
    std::vector<Country> countries;
    std::vector<std::string> genres;
    std::string error;
};
constexpr size_t DirectoryLimit = 80;
std::string browsePath(const BrowseQuery& query);
std::vector<DirectoryStation> parseDirectoryStations(std::string_view document);
DirectoryFilters parseDirectoryFilters(std::string_view countries, std::string_view genres);
} // namespace radio
