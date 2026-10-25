#pragma once
#include "core.h"
#include "directory.h"
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>

namespace radio {
struct SearchResult {
    uint64_t generation = 0;
    std::vector<DirectoryStation> stations;
    std::string error;
    bool cached = false;
};
// Bounded background requests; completed results are polled by the UI, never posted
// through raw pointers to windows which might have been destroyed.
class Network {
  public:
    explicit Network(fs::path cache,
                     std::vector<std::string> servers = {"https://all.api.radio-browser.info",
                                                         "https://de1.api.radio-browser.info"},
                     std::chrono::seconds exhaustedCooldown = std::chrono::minutes(5));
    ~Network();
    void search(BrowseQuery query);
    void requestFilters();
    std::optional<DirectoryFilters> takeFilters();
    void cancelSearch();
    void requestLogo(const std::string& url);
    fs::path logoPath(const std::string& url) const;
    std::optional<SearchResult> takeSearch();
    bool takeLogoChange();

  private:
    void run(std::stop_token stop);
    void trimCache();
    fs::path cache_;
    std::jthread worker_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::stop_source activeRequest_;
    std::optional<BrowseQuery> query_;
    std::optional<SearchResult> result_;
    struct LogoRequest {
        unsigned attempts = 0;
        std::chrono::steady_clock::time_point due{};
        std::chrono::steady_clock::time_point lastUsed{};
        bool queued = false;
    };
    std::deque<std::string> logos_;
    std::unordered_map<std::string, LogoRequest> logoRequests_;
    std::string activeLogo_;
    const std::chrono::seconds exhaustedCooldown_;
    uint64_t generation_ = 0;
    bool logosChanged_ = false;
    bool filtersRequested_ = false;
    std::optional<DirectoryFilters> filters_, cachedFilters_;
    const std::vector<std::string> servers_;
};
} // namespace radio
