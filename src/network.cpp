#include "network.h"
#include <stdexcept>
#include <windows.h>
#include <winhttp.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <atomic>
#include <cctype>
#include <memory>
#include <unordered_set>

namespace radio {
namespace {
struct Internet {
    HINTERNET h = nullptr;
    ~Internet() {
        if (h)
            WinHttpCloseHandle(h);
    }
    explicit operator bool() const {
        return h != nullptr;
    }
};
constexpr size_t LogoBytes = 1024 * 1024;
constexpr unsigned LogoAttempts = 3;
constexpr size_t DirectoryBytes = 2 * 1024 * 1024;
constexpr auto DirectoryCacheAge = std::chrono::hours(24 * 7);
constexpr char DirectoryCacheName[] = "last-directory-search.cache";
// A single recent search is enough to make an outage useful without retaining a
// growing history of typed station names or stale directory entries.
void saveDirectorySearch(const fs::path& cache, const std::string& path, const std::string& body) {
    try {
        atomicWrite(cache / DirectoryCacheName, path + '\n' + body, false);
    } catch (const std::exception&) {
        // A read-only/full cache must never turn a successful search into a failure.
    }
}
std::vector<DirectoryStation> loadDirectorySearch(const fs::path& cache, const std::string& path) {
    try {
        const auto file = cache / DirectoryCacheName;
        std::error_code error;
        const auto modified = fs::last_write_time(file, error);
        if (error || modified + DirectoryCacheAge < fs::file_time_type::clock::now())
            return {};
        const auto content = readFile(file, DirectoryBytes + 2048);
        const auto separator = content.find('\n');
        if (separator == std::string::npos || separator > 1024 ||
            content.compare(0, separator, path) != 0 || separator != path.size())
            return {};
        return parseDirectoryStations(std::string_view(content).substr(separator + 1));
    } catch (const std::exception&) {
        return {}; // Corrupt/locked optional cache; preserve the server error.
    }
}
struct Apartment {
    HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ~Apartment() {
        if (SUCCEEDED(result))
            CoUninitialize();
    }
};
bool usableImage(IWICImagingFactory* factory, std::string& bytes) {
    if (!factory || bytes.empty() || bytes.size() > LogoBytes)
        return false;
    using Microsoft::WRL::ComPtr;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICBitmapScaler> scaler;
    ComPtr<IWICFormatConverter> converter;
    UINT width{}, height{};
    BYTE pixel[4]{};
    return SUCCEEDED(factory->CreateStream(&stream)) &&
           SUCCEEDED(stream->InitializeFromMemory(reinterpret_cast<BYTE*>(bytes.data()),
                                                  static_cast<DWORD>(bytes.size()))) &&
           SUCCEEDED(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand,
                                                      &decoder)) &&
           SUCCEEDED(decoder->GetFrame(0, &frame)) && SUCCEEDED(frame->GetSize(&width, &height)) && width &&
           height && width <= 2048 && height <= 2048 && SUCCEEDED(factory->CreateBitmapScaler(&scaler)) &&
           SUCCEEDED(scaler->Initialize(frame.Get(), 1, 1, WICBitmapInterpolationModeFant)) &&
           SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
           SUCCEEDED(converter->Initialize(scaler.Get(), GUID_WICPixelFormat32bppPBGRA,
                                           WICBitmapDitherTypeNone, nullptr, 0,
                                           WICBitmapPaletteTypeCustom)) &&
           SUCCEEDED(converter->CopyPixels(nullptr, 4, sizeof(pixel), pixel));
}
// WinHTTP retains callback context and the read buffer until HANDLE_CLOSING.
// The worker owns the other reference, so cancelling never leaves a callback
// pointing at a destroyed stack frame. Only the worker calls request APIs.
struct HttpState {
    std::atomic<unsigned> references{1};
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    std::atomic<DWORD> completion{0}, error{0}, bytes{0};
    char buffer[8192]{};
    ~HttpState() {
        if (event)
            CloseHandle(event);
    }
    void release() noexcept {
        if (references.fetch_sub(1) == 1)
            delete this;
    }
    static void CALLBACK callback(HINTERNET, DWORD_PTR context, DWORD status, void* info,
                                  DWORD length) noexcept {
        auto* state = reinterpret_cast<HttpState*>(context);
        if (!state)
            return;
        if (status == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING) {
            state->release();
            return;
        }
        if (status == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR)
            state->error = static_cast<WINHTTP_ASYNC_RESULT*>(info)->dwError;
        else if (status == WINHTTP_CALLBACK_STATUS_READ_COMPLETE)
            state->bytes = length;
        else if (status != WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE &&
                 status != WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE)
            return;
        state->completion = status;
        SetEvent(state->event);
    }
};
class HttpRequest {
  public:
    explicit HttpRequest(HINTERNET handle) : handle_(handle) {
        try {
            if (!handle_)
                throw std::runtime_error("Request failed.");
            state_ = new HttpState;
            if (!state_->event)
                throw std::runtime_error("Cannot allocate a network completion event.");
            auto context = reinterpret_cast<DWORD_PTR>(state_);
            if (!WinHttpSetOption(handle_, WINHTTP_OPTION_CONTEXT_VALUE, &context, sizeof(context)) ||
                WinHttpSetStatusCallback(handle_, HttpState::callback,
                                         WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS |
                                             WINHTTP_CALLBACK_FLAG_HANDLES,
                                         0) == WINHTTP_INVALID_STATUS_CALLBACK)
                throw std::runtime_error("Cannot initialize network completion handling.");
            ++state_->references; // Released by the final HANDLE_CLOSING notification.
        } catch (...) {
            if (handle_)
                WinHttpCloseHandle(handle_);
            if (state_)
                state_->release();
            throw;
        }
    }
    ~HttpRequest() {
        WinHttpCloseHandle(handle_); // Cancels any outstanding asynchronous operation.
        state_->release();
    }
    HttpRequest(const HttpRequest&) = delete;
    HttpRequest& operator=(const HttpRequest&) = delete;
    HINTERNET handle() const {
        return handle_;
    }
    HttpState& state() const {
        return *state_;
    }
    void wait(BOOL initiated, DWORD expected, std::chrono::steady_clock::time_point deadline,
              std::stop_token stop, std::stop_token cancelled) {
        if (!initiated)
            throw std::runtime_error("Network operation failed (Windows error " +
                                     std::to_string(GetLastError()) + ").");
        for (;;) {
            if (stop.stop_requested() || cancelled.stop_requested())
                throw std::runtime_error("Cancelled.");
            if (std::chrono::steady_clock::now() >= deadline)
                throw std::runtime_error("Request timed out.");
            const auto result = WaitForSingleObject(state_->event, 50);
            if (result == WAIT_TIMEOUT)
                continue;
            if (result != WAIT_OBJECT_0)
                throw std::runtime_error("Network completion wait failed.");
            if (state_->completion == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR)
                throw std::runtime_error("Network request failed (Windows error " +
                                         std::to_string(state_->error.load()) + ").");
            if (state_->completion != expected)
                throw std::runtime_error("Unexpected network completion.");
            return;
        }
    }

  private:
    HINTERNET handle_{};
    HttpState* state_{};
};
std::string get(const std::string& url, size_t limit, std::stop_token stop, std::stop_token cancelled) {
    if (stop.stop_requested() || cancelled.stop_requested())
        throw std::runtime_error("Cancelled.");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(12);
    if (!validUrl(url))
        throw std::runtime_error("Invalid URL.");
    auto w = wide(url);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUrlPathLength = static_cast<DWORD>(-1);
    parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(w.c_str(), 0, 0, &parts))
        throw std::runtime_error("Invalid URL.");
    std::wstring host(parts.lpszHostName, parts.dwHostNameLength),
        path(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.dwExtraInfoLength)
        path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    if (path.empty())
        path = L"/";
    Internet session{WinHttpOpen(L"QuickStation/1.6.14 (native desktop player)",
                                 WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, nullptr, nullptr, WINHTTP_FLAG_ASYNC)};
    if (!session)
        throw std::runtime_error("Network initialization failed.");
    WinHttpSetTimeouts(session.h, 3000, 3000, 3000, 3000);
    Internet connect{WinHttpConnect(session.h, host.c_str(), parts.nPort, 0)};
    if (!connect)
        throw std::runtime_error("Connection failed.");
    HttpRequest request{WinHttpOpenRequest(connect.h, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)};
    DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    if (!WinHttpSetOption(request.handle(), WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect)))
        throw std::runtime_error("Cannot configure secure redirect handling.");
    request.wait(WinHttpSendRequest(request.handle(), WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                    WINHTTP_NO_REQUEST_DATA, 0, 0,
                                    reinterpret_cast<DWORD_PTR>(&request.state())),
                 WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE, deadline, stop, cancelled);
    request.wait(WinHttpReceiveResponse(request.handle(), nullptr), WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE,
                 deadline, stop, cancelled);
    DWORD status = 0, length = sizeof(status);
    if (!WinHttpQueryHeaders(request.handle(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr,
                             &status, &length, nullptr) ||
        status != 200)
        throw std::runtime_error("Server returned HTTP " + std::to_string(status) + ".");
    std::string body;
    for (;;) {
        request.wait(WinHttpReadData(request.handle(), request.state().buffer, sizeof(request.state().buffer),
                                     nullptr),
                     WINHTTP_CALLBACK_STATUS_READ_COMPLETE, deadline, stop, cancelled);
        const DWORD n = request.state().bytes;
        if (!n)
            return body;
        if (body.size() + n > limit)
            throw std::runtime_error("Response is too large.");
        body.append(request.state().buffer, n);
    }
}
} // namespace
Network::Network(fs::path cache, std::vector<std::string> servers,
                 std::chrono::seconds exhaustedCooldown)
    : cache_(std::move(cache)), exhaustedCooldown_(exhaustedCooldown), servers_(std::move(servers)) {
    fs::create_directories(cache_);
    worker_ = std::jthread([this](std::stop_token stop) { run(stop); });
}
Network::~Network() {
    {
        std::lock_guard lock(mutex_);
        worker_.request_stop();
    }
    wake_.notify_all();
    if (worker_.joinable())
        worker_.join();
}
void Network::search(BrowseQuery query) {
    std::lock_guard lock(mutex_);
    activeRequest_.request_stop();
    query_ = std::move(query);
    result_.reset();
    ++generation_;
    wake_.notify_one();
}
void Network::requestFilters() {
    std::lock_guard lock(mutex_);
    if (cachedFilters_) {
        filters_ = cachedFilters_;
        return;
    }
    filtersRequested_ = true;
    wake_.notify_one();
}
std::optional<DirectoryFilters> Network::takeFilters() {
    std::lock_guard lock(mutex_);
    auto result = std::move(filters_);
    filters_.reset();
    return result;
}
void Network::cancelSearch() {
    std::lock_guard lock(mutex_);
    activeRequest_.request_stop();
    query_.reset();
    result_.reset();
    ++generation_;
}
void Network::requestLogo(const std::string& url) {
    if (url.empty() || !validUrl(url))
        return;
    std::lock_guard lock(mutex_);
    // File validation and retries belong to the worker, never the UI thread.
    constexpr size_t requestLimit = 1024;
    const auto now = std::chrono::steady_clock::now();
    if (auto found = logoRequests_.find(url); found != logoRequests_.end()) {
        found->second.lastUsed = now;
        return;
    }
    if (logoRequests_.size() >= requestLimit) {
        auto oldest = logoRequests_.end();
        for (auto it = logoRequests_.begin(); it != logoRequests_.end(); ++it)
            if (it->first != activeLogo_ &&
                (oldest == logoRequests_.end() || it->second.lastUsed < oldest->second.lastUsed))
                oldest = it;
        if (oldest == logoRequests_.end())
            return;
        if (oldest->second.queued)
            std::erase(logos_, oldest->first);
        logoRequests_.erase(oldest);
    }
    auto [it, inserted] = logoRequests_.try_emplace(url);
    if (inserted) {
        it->second.lastUsed = now;
        it->second.queued = true;
        logos_.push_back(url);
        wake_.notify_one();
    }
}
fs::path Network::logoPath(const std::string& url) const {
    return cache_ / (cacheKey(url) + ".img");
}
std::optional<SearchResult> Network::takeSearch() {
    std::lock_guard lock(mutex_);
    auto result = std::move(result_);
    result_.reset();
    return result;
}
bool Network::takeLogoChange() {
    std::lock_guard lock(mutex_);
    bool changed = logosChanged_;
    logosChanged_ = false;
    return changed;
}
void Network::trimCache() {
    constexpr uintmax_t byteLimit = 48ull * 1024 * 1024;
    constexpr size_t fileLimit = 1024;
    struct Entry {
        fs::path path;
        uintmax_t bytes;
        fs::file_time_type modified;
    };
    std::unordered_set<std::string> protectedKeys;
    {
        std::lock_guard lock(mutex_);
        const auto recent = std::chrono::steady_clock::now() - std::chrono::minutes(2);
        for (const auto& [url, request] : logoRequests_)
            if (request.queued || request.lastUsed >= recent || url == activeLogo_)
                protectedKeys.insert(cacheKey(url));
    }
    std::vector<Entry> entries;
    uintmax_t bytes = 0;
    size_t files = 0;
    std::error_code ec;
    for (fs::directory_iterator it(cache_, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        const auto& path = it->path();
        const auto name = path.stem().string();
        if (path.extension() != L".img" || name.size() != 16 ||
            !std::all_of(name.begin(), name.end(), [](unsigned char c) { return std::isxdigit(c); }) ||
            !it->is_regular_file(ec))
            continue;
        const auto size = it->file_size(ec);
        const auto modified = it->last_write_time(ec);
        if (ec) {
            ec.clear();
            continue;
        }
        ++files;
        bytes += size;
        if (!protectedKeys.contains(name))
            entries.push_back({path, size, modified});
    }
    std::sort(entries.begin(), entries.end(),
              [](const Entry& a, const Entry& b) { return a.modified < b.modified; });
    const auto old = fs::file_time_type::clock::now() - std::chrono::hours(24 * 30);
    std::unordered_set<std::string> evictedKeys;
    for (const auto& entry : entries) {
        if (entry.modified >= old && files <= fileLimit && bytes <= byteLimit)
            break;
        ec.clear();
        if (fs::remove(entry.path, ec)) {
            evictedKeys.insert(entry.path.stem().string());
            --files;
            bytes -= entry.bytes;
        }
    }
    if (!evictedKeys.empty()) {
        std::lock_guard lock(mutex_);
        for (auto it = logoRequests_.begin(); it != logoRequests_.end();) {
            if (!it->second.queued && it->first != activeLogo_ &&
                evictedKeys.contains(cacheKey(it->first)))
                it = logoRequests_.erase(it);
            else
                ++it;
        }
        logosChanged_ = true;
    }
}
void Network::run(std::stop_token stop) {
    Apartment apartment;
    Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
    if (SUCCEEDED(apartment.result))
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    trimCache();
    unsigned completedLogos = 0;
    while (!stop.stop_requested()) {
        std::unique_lock lock(mutex_);
        if (stop.stop_requested())
            break;
        const auto now = std::chrono::steady_clock::now();
        const auto ready = std::find_if(logos_.begin(), logos_.end(),
                                        [&](const auto& url) { return logoRequests_.at(url).due <= now; });
        if (!query_ && !filtersRequested_ && ready == logos_.end()) {
            if (logos_.empty())
                wake_.wait(lock);
            else {
                auto next = std::chrono::steady_clock::time_point::max();
                for (const auto& url : logos_)
                    next = std::min(next, logoRequests_.at(url).due);
                wake_.wait_until(lock, next);
            }
            continue;
        }
        activeRequest_ = std::stop_source{};
        const auto cancelled = activeRequest_.get_token();
        if (query_) {
            auto query = std::move(*query_);
            query_.reset();
            auto gen = generation_;
            lock.unlock();
            SearchResult result;
            result.generation = gen;
            std::string path;
            try {
                path = browsePath(query);
            } catch (const std::exception& e) {
                result.error = e.what();
            }
            for (const auto& server : servers_) {
                if (path.empty())
                    break;
                if (stop.stop_requested())
                    break;
                {
                    std::lock_guard guard(mutex_);
                    if (gen != generation_)
                        break;
                }
                try {
                    const auto body = get(server + path, DirectoryBytes, stop, cancelled);
                    result.stations = parseDirectoryStations(body);
                    result.error.clear();
                    if (!result.stations.empty() && !stop.stop_requested() && !cancelled.stop_requested())
                        saveDirectorySearch(cache_, path, body);
                    break;
                } catch (const std::exception& e) {
                    result.error = e.what();
                }
            }
            if (!path.empty() && !result.error.empty() && !stop.stop_requested() &&
                !cancelled.stop_requested()) {
                result.stations = loadDirectorySearch(cache_, path);
                result.cached = !result.stations.empty();
            }
            lock.lock();
            if (gen == generation_)
                result_ = std::move(result);
        } else if (filtersRequested_) {
            filtersRequested_ = false;
            lock.unlock();
            DirectoryFilters result;
            for (const auto& server : servers_) {
                if (stop.stop_requested() || cancelled.stop_requested())
                    break;
                try {
                    const auto countries = get(server + "/json/countrycodes?hidebroken=true&limit=300",
                                               2 * 1024 * 1024, stop, cancelled);
                    const auto genres =
                        get(server + "/json/tags?hidebroken=true&order=stationcount&reverse=true&limit=200",
                            2 * 1024 * 1024, stop, cancelled);
                    result = parseDirectoryFilters(countries, genres);
                    break;
                } catch (const std::exception& e) {
                    result.error = e.what();
                }
            }
            lock.lock();
            if (cancelled.stop_requested()) {
                filtersRequested_ = true; // Retry after the higher-priority search finishes.
                continue;
            }
            if (result.error.empty() && !stop.stop_requested())
                cachedFilters_ = result;
            filters_ = std::move(result);
        } else if (ready != logos_.end()) {
            auto url = std::move(*ready);
            logos_.erase(ready);
            auto& request = logoRequests_.at(url);
            request.queued = false;
            activeLogo_ = url;
            const bool inspectCache = request.attempts == 0;
            lock.unlock();
            bool complete = false;
            try {
                if (!factory)
                    throw std::runtime_error("Windows image decoder unavailable.");
                const auto path = logoPath(url);
                if (inspectCache) {
                    try {
                        auto cached = readFile(path, LogoBytes);
                        complete = usableImage(factory.Get(), cached);
                    } catch (const std::exception&) {
                    }
                    if (!complete) {
                        std::error_code ignored;
                        fs::remove(path, ignored); // Discard cached HTML, corrupt or unsupported images.
                    }
                }
                if (!complete && !stop.stop_requested()) {
                    auto data = get(url, LogoBytes, stop, cancelled);
                    if (!usableImage(factory.Get(), data))
                        throw std::runtime_error("Unusable station image.");
                    if (!stop.stop_requested()) {
                        atomicWrite(path, data, false);
                        complete = true;
                    }
                }
            } catch (const std::exception&) {
            }
            lock.lock();
            activeLogo_.clear();
            if (cancelled.stop_requested() && !complete) {
                request.queued = true;
                logos_.push_back(std::move(url));
                continue; // A search preempting a logo does not consume a retry.
            }
            ++request.attempts;
            if (complete) {
                logosChanged_ = true;
                if (++completedLogos % 32 == 0) {
                    lock.unlock();
                    trimCache();
                    lock.lock();
                }
            } else if (factory && !stop.stop_requested()) {
                if (request.attempts >= LogoAttempts) {
                    request.attempts = 0;
                    request.due = std::chrono::steady_clock::now() + exhaustedCooldown_;
                } else
                    request.due = std::chrono::steady_clock::now() + std::chrono::seconds(2 * request.attempts);
                request.queued = true;
                logos_.push_back(std::move(url));
            }
        }
    }
}
} // namespace radio
