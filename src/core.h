#pragma once
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace radio {
namespace fs = std::filesystem;
constexpr size_t MaxStations = 5000;
constexpr size_t MaxDocumentBytes = 8 * 1024 * 1024;
struct Station {
    std::string id, name, url, logo, genre;
    bool favorite = true;
};
struct Library {
    std::vector<Station> stations;
    int volume = 65;
    bool muted = false;
    bool doubleClick = false;
    std::string lastStation;
};
std::wstring wide(std::string_view value);
std::string utf8(std::wstring_view value);
std::string trim(std::string value);
bool validUrl(std::string_view value);
void validate(const Station& station);
std::string newId();
std::string encode(const Library& library);
Library decode(std::string_view document);
std::vector<Station> parsePlaylist(std::string_view document);
std::string writePlaylist(const std::vector<Station>& stations);
size_t mergeStations(Library& library, const std::vector<Station>& stations);
std::string metadataTitle(std::string_view metadata);
std::chrono::seconds retryDelay(unsigned failures);
std::string cacheKey(std::string_view url);
Library starterLibrary();
std::string readFile(const fs::path& path, size_t limit = MaxDocumentBytes);
void atomicWrite(const fs::path& path, std::string_view content, bool backup = true);
class Store {
  public:
    explicit Store(fs::path directory);
    Library load();
    void save(const Library& library);
    const fs::path& directory() const {
        return directory_;
    }
    const std::string& warning() const {
        return warning_;
    }

  private:
    fs::path directory_;
    std::string warning_;
};
} // namespace radio
