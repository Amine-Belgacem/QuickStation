#include "network.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <atomic>
#include <iostream>
#include <mutex>
#include <thread>
#include <functional>
#include <cstring>
#include <fstream>

using namespace radio;
using namespace std::chrono_literals;
namespace {
int checks = 0;
void check(bool value, const char* message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
void rejects(const std::function<void()>& operation, const char* message) {
    bool rejected = false;
    try {
        operation();
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected, message);
}
std::string logoBitmap() {
    BITMAPFILEHEADER file{};
    BITMAPINFOHEADER info{};
    file.bfType = 0x4d42;
    file.bfOffBits = sizeof(file) + sizeof(info);
    file.bfSize = file.bfOffBits + 16;
    info.biSize = sizeof(info);
    info.biWidth = info.biHeight = 2;
    info.biPlanes = 1;
    info.biBitCount = 32;
    std::string bytes(file.bfSize, static_cast<char>(0xff));
    memcpy(bytes.data(), &file, sizeof(file));
    memcpy(bytes.data() + sizeof(file), &info, sizeof(info));
    return bytes;
}
class Server {
  public:
    SOCKET socket = INVALID_SOCKET;
    unsigned short port{};
    std::jthread thread;
    std::atomic<bool> fail{false}, slow{false};
    std::atomic<int> connections{0}, cancelled{0};
    std::mutex mutex;
    std::vector<std::string> requests;
    Server() {
        socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (socket == INVALID_SOCKET ||
            bind(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR ||
            listen(socket, 4) == SOCKET_ERROR)
            throw std::runtime_error("Cannot start directory fixture.");
        int size = sizeof(address);
        getsockname(socket, reinterpret_cast<sockaddr*>(&address), &size);
        port = ntohs(address.sin_port);
        thread = std::jthread([this](std::stop_token stop) { serve(stop); });
    }
    ~Server() {
        thread.request_stop();
        closesocket(socket);
        if (thread.joinable())
            thread.join();
    }
    std::string url() const {
        return "http://127.0.0.1:" + std::to_string(port);
    }
    void serve(std::stop_token stop) {
        while (!stop.stop_requested()) {
            auto client = accept(socket, nullptr, nullptr);
            if (client == INVALID_SOCKET)
                break;
            DWORD timeout = 1000;
            setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char*>(&timeout), sizeof(timeout));
            setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<char*>(&timeout), sizeof(timeout));
            char buffer[4096]{};
            const auto received = recv(client, buffer, sizeof(buffer), 0);
            const std::string request = received > 0 ? std::string(buffer, received) : std::string{};
            {
                std::lock_guard guard(mutex);
                requests.push_back(request);
            }
            const bool failRequest = fail.load();
            ++connections;
            if (request.find("name=stall") != std::string::npos) {
                // Accept the request but never send headers. Detect real cancellation.
                u_long nonblocking = 1;
                ioctlsocket(client, FIONBIO, &nonblocking);
                while (!stop.stop_requested()) {
                    const int n = recv(client, buffer, sizeof(buffer), 0);
                    if (n == 0 || (n == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK)) {
                        ++cancelled;
                        break;
                    }
                    std::this_thread::sleep_for(10ms);
                }
                closesocket(client);
                continue;
            }
            if (request.find("name=dribble") != std::string::npos) {
                const char header[] = "HTTP/1.0 200 OK\r\nContent-Length: 999999\r\n\r\n";
                send(client, header, sizeof(header) - 1, 0);
                while (!stop.stop_requested() && send(client, " ", 1, 0) > 0)
                    std::this_thread::sleep_for(20ms);
                closesocket(client);
                continue;
            }
            if (slow)
                for (int i = 0; i < 30 && !stop.stop_requested(); ++i)
                    std::this_thread::sleep_for(10ms);
            std::string body;
            if (request.find("/logo") != std::string::npos)
                body = logoBitmap();
            else if (request.find("/countrycodes") != std::string::npos)
                body = R"([{"name":"TN"},{"name":"FR"}])";
            else if (request.find("/tags") != std::string::npos)
                body = R"([{"name":"jazz"},{"name":"news"}])";
            else if (request.find("name=empty") != std::string::npos)
                body = "[]";
            else if (request.find("name=oversized") != std::string::npos)
                body = std::string(2 * 1024 * 1024 + 1, ' ');
            else
                body =
                    R"([{"name":"Fixture Jazz","url_resolved":"http://127.0.0.1:1234/radio","country":"Tunisia","tags":"jazz"}])";
            const std::string response =
                failRequest ? "HTTP/1.0 503 Unavailable\r\nContent-Length: 0\r\n\r\n"
                            : "HTTP/1.0 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
                                  std::to_string(body.size()) + "\r\n\r\n" + body;
            for (size_t offset = 0; offset < response.size();) {
                const auto sent =
                    send(client, response.data() + offset, static_cast<int>(response.size() - offset), 0);
                if (sent <= 0)
                    break;
                offset += sent;
            }
            shutdown(client, SD_BOTH);
            closesocket(client);
        }
    }
};
template <class T>
T waitFor(const std::function<std::optional<T>()>& poll, std::chrono::seconds timeout = 5s) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (auto value = poll())
            return std::move(*value);
        std::this_thread::sleep_for(10ms);
    }
    throw std::runtime_error("Directory fixture timed out.");
}
} // namespace
int main(int argc, char** argv) {
    WSADATA sockets{};
    if (WSAStartup(MAKEWORD(2, 2), &sockets))
        return 1;
    const auto root = fs::current_path() / "test-data" / ("directory-tests-" + newId());
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--live") {
            Network network(root);
            network.requestFilters();
            const auto result = waitFor<DirectoryFilters>([&] { return network.takeFilters(); }, 30s);
            std::cout << "Live directory: " << result.countries.size() << " countries, "
                      << result.genres.size() << " genres. " << result.error << '\n';
            network.search({"bloomberg", "", ""});
            const auto search = waitFor<SearchResult>([&] { return network.takeSearch(); }, 30s);
            std::cout << "Live Bloomberg search: " << search.stations.size() << " stations. "
                      << search.error << '\n';
            const bool success = result.error.empty() && search.error.empty() && !search.stations.empty();
            fs::remove_all(root);
            return success ? 0 : 1;
        }
        check(browsePath({}).find("&name=") == std::string::npos,
              "Browse supports empty name and all filters");
        const auto path = browsePath({"Jazz & café", "TN", "jazz / blues"});
        check(path.find("name=Jazz%20%26%20caf%C3%A9") != std::string::npos,
              "Unicode name and reserved characters encoded");
        check(path.find("countrycode=TN") != std::string::npos &&
                  path.find("tag=jazz%20%2F%20blues&tagExact=true") != std::string::npos,
              "Country and exact genre combined");
        rejects([] { browsePath({"", "TN&name=other", ""}); }, "Country cannot inject parameters");
        rejects([] { browsePath({std::string(257, 'a'), "", ""}); }, "Input bounded");
        rejects([] { parseDirectoryStations("{}"); }, "Directory shape validated");
        rejects([] { parseDirectoryStations(std::string(2 * 1024 * 1024 + 1, ' ')); },
                "Directory size bounded");
        std::string deep(25, '[');
        deep += "0";
        deep += std::string(25, ']');
        rejects([&] { parseDirectoryStations(deep); }, "Directory depth bounded");
        const auto stations = parseDirectoryStations(
            R"([{"name":"One","url_resolved":"https://test/one","country":"Tunisia","tags":"jazz"},{"name":"duplicate","url_resolved":"https://test/one"},{"name":"bad","url_resolved":"file:///tmp/a"},{"name":123},{"name":"Fallback URL","url":"https://test/two","favicon":"invalid"}])");
        check(stations.size() == 2 && stations[0].country == "Tunisia" && stations[1].station.logo.empty(),
              "Malformed entries skipped, duplicate streams removed, URL fallback supported");
        std::string many = "[";
        for (int i = 0; i < 100; ++i) {
            if (i)
                many += ',';
            many += "{\"name\":\"Test\",\"url_resolved\":\"https://test/" + std::to_string(i) + "\"}";
        }
        many += ']';
        check(parseDirectoryStations(many).size() == 80, "Result count bounded");
        const auto filters =
            parseDirectoryFilters(R"([{"name":"TN"},{"name":"TN"},{"name":"bad"},{"name":"FR"}])",
                                  R"([{"name":"jazz"},{"name":"news"},{"name":"jazz"},{"name":""}])");
        check(filters.countries.size() == 2 && filters.genres.size() == 2,
              "Filter entries validated and deduplicated");
        {
            Server primary, backup;
            primary.fail = true;
            Network network(root, {primary.url(), backup.url()});
            network.search({"Jazz & café", "TN", "jazz"});
            const auto result = waitFor<SearchResult>([&] { return network.takeSearch(); });
            check(result.error.empty() && result.stations.size() == 1 && primary.connections > 0 &&
                      backup.connections > 0,
                  "Directory failover produces valid results");
            {
                std::lock_guard guard(backup.mutex);
                check(backup.requests.back().find("countrycode=TN") != std::string::npos,
                      "Real HTTP request includes chosen country");
            }
            network.requestFilters();
            const auto loaded = waitFor<DirectoryFilters>([&] { return network.takeFilters(); });
            check(loaded.error.empty() && loaded.countries.size() == 2 && loaded.genres.size() == 2,
                  "Countries and genres loaded in worker");
            const int before = backup.connections;
            network.requestFilters();
            check(waitFor<DirectoryFilters>([&] { return network.takeFilters(); }).countries.size() == 2 &&
                      backup.connections == before,
                  "Successful filters reused without another download");
            network.search({"empty", "", ""});
            check(waitFor<SearchResult>([&] { return network.takeSearch(); }).stations.empty(),
                  "Empty results are a successful response");
            primary.fail = false;
            primary.slow = true;
            const int started = primary.connections;
            network.search({"old", "", ""});
            const auto deadline = std::chrono::steady_clock::now() + 2s;
            while (primary.connections == started && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(10ms);
            check(primary.connections > started, "Slow in-flight request started");
            network.cancelSearch();
            std::this_thread::sleep_for(500ms);
            check(!network.takeSearch(), "Cancelled response cannot revive UI results");
            primary.slow = false;
            primary.fail = true;
            backup.fail = true;
            network.search({});
            check(!waitFor<SearchResult>([&] { return network.takeSearch(); }).error.empty(),
                  "Both servers offline produce an error");
            const BrowseQuery original{"Jazz & café", "TN", "jazz"};
            network.search(original);
            const auto cached = waitFor<SearchResult>([&] { return network.takeSearch(); });
            check(cached.cached && !cached.error.empty() && cached.stations.size() == 1,
                  "Offline search reuses only the matching recent successful result");
            Network reopened(root, {primary.url(), backup.url()});
            reopened.search(original);
            check(waitFor<SearchResult>([&] { return reopened.takeSearch(); }).cached,
                  "Recent search survives restart for an offline directory");
            reopened.search({"different", "", ""});
            const auto different = waitFor<SearchResult>([&] { return reopened.takeSearch(); });
            check(!different.cached && different.stations.empty() && !different.error.empty(),
                  "Cached results cannot appear for another search");
            const auto cache = root / "last-directory-search.cache";
            fs::last_write_time(cache, fs::file_time_type::clock::now() - std::chrono::hours(24 * 8));
            reopened.search(original);
            check(!waitFor<SearchResult>([&] { return reopened.takeSearch(); }).cached,
                  "Search results older than seven days are not offered offline");
            atomicWrite(cache, "corrupt", false);
            reopened.search(original);
            check(!waitFor<SearchResult>([&] { return reopened.takeSearch(); }).cached,
                  "Corrupt optional cache cannot break offline search");
        }
        {
            Server server;
            const auto url = server.url() + "/logo-cache";
            {
                Network network(root / "logos", {}, 1s);
                atomicWrite(network.logoPath(url), "<html>old server error</html>", false);
                network.requestLogo(url);
                waitFor<bool>([&]() -> std::optional<bool> {
                    return network.takeLogoChange() ? std::optional<bool>{true} : std::nullopt;
                });
                check(readFile(network.logoPath(url)) == logoBitmap(),
                      "Invalid cached logo replaced by a validated image");
                const int before = server.connections;
                for (int i = 0; i < 100; ++i)
                    network.requestLogo(url);
                std::this_thread::sleep_for(100ms);
                check(server.connections == before, "Repeated UI polls do not redownload a loaded logo");
                const auto retry = server.url() + "/logo-retry";
                server.fail = true;
                network.requestLogo(retry);
                const auto deadline = std::chrono::steady_clock::now() + 2s;
                while (server.connections == before && std::chrono::steady_clock::now() < deadline)
                    std::this_thread::sleep_for(10ms);
                check(server.connections > before, "Initial logo download failed in loopback fixture");
                server.fail = false;
                waitFor<bool>(
                    [&]() -> std::optional<bool> {
                        return network.takeLogoChange() ? std::optional<bool>{true} : std::nullopt;
                    },
                    8s);
                check(readFile(network.logoPath(retry)) == logoBitmap() && server.connections >= before + 2,
                      "Transient logo failure retries and recovers without a UI refresh or restart");
                server.fail = true;
                const int failingBefore = server.connections;
                const auto failed = server.url() + "/logo-exhausted";
                network.requestLogo(failed);
                const auto bound = std::chrono::steady_clock::now() + 9s;
                while (server.connections < failingBefore + 3 && std::chrono::steady_clock::now() < bound)
                    std::this_thread::sleep_for(10ms);
                check(server.connections == failingBefore + 3 && !fs::exists(network.logoPath(failed)),
                      "Unreachable logo has exactly three bounded attempts and no poisoned cache");
                for (int i = 0; i < 100; ++i)
                    network.requestLogo(failed);
                std::this_thread::sleep_for(100ms);
                check(server.connections == failingBefore + 3,
                      "Polling cannot restart an exhausted logo request");
                server.fail = false;
                check(waitFor<bool>([&]() -> std::optional<bool> {
                          return fs::exists(network.logoPath(failed)) &&
                                         readFile(network.logoPath(failed)) == logoBitmap()
                                     ? std::optional<bool>{true} : std::nullopt;
                      }, 4s) && server.connections >= failingBefore + 4,
                      "Exhausted logo retries after cooldown and recovers without restart");
            }
            const int cachedBefore = server.connections;
            Network reopened(root / "logos");
            reopened.requestLogo(url);
            waitFor<bool>([&]() -> std::optional<bool> {
                return reopened.takeLogoChange() ? std::optional<bool>{true} : std::nullopt;
            });
            check(server.connections == cachedBefore, "Validated cached logo survives offline relaunch");
        }
        {
            Server server;
            const auto cache = root / "cache-budget";
            fs::create_directories(cache);
            const auto evicted = server.url() + "/logo-evicted";
            const auto old = cache / (cacheKey(evicted) + ".img");
            atomicWrite(old, "stale", false);
            fs::last_write_time(old, fs::file_time_type::clock::now() - std::chrono::hours(24 * 40));
            std::ofstream(cache / "notes.txt") << "keep";
            for (unsigned i = 0; i < 1030; ++i) {
                wchar_t name[32]{};
                swprintf_s(name, L"%016llx.img", static_cast<unsigned long long>(i));
                std::ofstream(cache / name) << "cache entry";
            }
            Network network(cache);
            check(waitFor<bool>([&]() -> std::optional<bool> {
                      size_t count = 0;
                      for (const auto& entry : fs::directory_iterator(cache))
                          count += entry.path().extension() == L".img";
                      return count <= 1024 ? std::optional<bool>{true} : std::nullopt;
                  }, 5s) && !fs::exists(old) && fs::exists(cache / "notes.txt"),
                  "Background cache cleanup bounds entries, removes stale logos and preserves other files");
            network.requestLogo(evicted);
            check(waitFor<bool>([&]() -> std::optional<bool> {
                      return fs::exists(old) && readFile(old) == logoBitmap()
                                 ? std::optional<bool>{true} : std::nullopt;
                  }, 5s),
                  "Evicted logo can be downloaded again without changing station data");
        }
        {
            Server server;
            auto network = std::make_unique<Network>(root / "cancellation", std::vector{server.url()});
            auto waitConnection = [&](int before) {
                const auto deadline = std::chrono::steady_clock::now() + 2s;
                while (server.connections == before && std::chrono::steady_clock::now() < deadline)
                    std::this_thread::sleep_for(10ms);
                check(server.connections > before, "Stalled HTTP request reached the server");
            };
            network->search({"stall", "", ""});
            waitConnection(0);
            const auto replaced = std::chrono::steady_clock::now();
            network->search({"empty", "", ""});
            const auto result = waitFor<SearchResult>([&] { return network->takeSearch(); }, 2s);
            check(result.error.empty() && result.stations.empty() && server.cancelled == 1 &&
                      std::chrono::steady_clock::now() - replaced < 2s,
                  "Replacement closes the stale socket and completes without its receive timeout");
            const int before = server.connections;
            network->search({"stall", "", ""});
            waitConnection(before);
            const auto closed = std::chrono::steady_clock::now();
            network.reset();
            check(std::chrono::steady_clock::now() - closed < 1s,
                  "Network shutdown is responsive during a request with withheld headers");
            Network bounded(root / "deadline", {server.url()});
            const auto started = std::chrono::steady_clock::now();
            bounded.search({"dribble", "", ""});
            const auto timed = waitFor<SearchResult>([&] { return bounded.takeSearch(); }, 15s);
            check(!timed.error.empty() && std::chrono::steady_clock::now() - started < 14s,
                  "Total HTTP deadline stops a response that continually sends tiny amounts of data");
            bounded.search({"oversized", "", ""});
            check(!waitFor<SearchResult>([&] { return bounded.takeSearch(); }).error.empty(),
                  "Actual HTTP response size is bounded before parsing");
            bounded.search({"empty", "", ""});
            check(waitFor<SearchResult>([&] { return bounded.takeSearch(); }).error.empty(),
                  "Directory worker remains usable after timeouts and oversized responses");
        }
        fs::remove_all(root);
        WSACleanup();
        std::cout << "PASS: " << checks << " directory checks\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        WSACleanup();
        return 1;
    }
}
