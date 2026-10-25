#include "core.h"
#include <fstream>
#include <stdexcept>
#include <windows.h>

namespace radio {
namespace {
struct File {
    HANDLE h = INVALID_HANDLE_VALUE;
    ~File() {
        if (h != INVALID_HANDLE_VALUE)
            CloseHandle(h);
    }
};
[[noreturn]] void fileError(const char* action) {
    throw std::runtime_error(std::string(action) + " (Windows error " + std::to_string(GetLastError()) +
                             ").");
}
} // namespace
std::string readFile(const fs::path& path, size_t limit) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        throw std::runtime_error("Cannot open file: " + utf8(path.filename().wstring()));
    std::string out;
    char buffer[8192];
    while (stream) {
        stream.read(buffer, sizeof(buffer));
        auto n = static_cast<size_t>(stream.gcount());
        if (out.size() + n > limit)
            throw std::runtime_error("File exceeds the size limit.");
        out.append(buffer, n);
    }
    if (!stream.eof())
        throw std::runtime_error("Could not read the complete file.");
    return out;
}
void atomicWrite(const fs::path& path, std::string_view content, bool backup) {
    fs::create_directories(path.parent_path());
    auto temp = path;
    temp += L".tmp-" + wide(newId());
    try {
        {
            File f{CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL,
                               nullptr)};
            if (f.h == INVALID_HANDLE_VALUE)
                fileError("Cannot create save file");
            size_t written = 0;
            while (written < content.size()) {
                DWORD n = 0,
                      count = static_cast<DWORD>(std::min<size_t>(content.size() - written, 1024 * 1024));
                if (!WriteFile(f.h, content.data() + written, count, &n, nullptr) || !n)
                    fileError("Cannot write save file");
                written += n;
            }
            if (!FlushFileBuffers(f.h))
                fileError("Cannot flush save file");
        }
        if (fs::exists(path) && backup) {
            auto previous = path;
            previous += L".bak";
            if (!ReplaceFileW(path.c_str(), temp.c_str(), previous.c_str(), 0, nullptr, nullptr))
                fileError("Cannot replace save file");
        } else if (!MoveFileExW(temp.c_str(), path.c_str(),
                                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            fileError("Cannot finalize save file");
    } catch (...) {
        std::error_code ec;
        fs::remove(temp, ec);
        throw;
    }
}
Store::Store(fs::path path) : directory_(std::move(path)) {
    fs::create_directories(directory_);
}
Library Store::load() {
    auto path = directory_ / L"stations.json";
    if (!fs::exists(path)) {
        if (fs::exists(directory_ / L"stations.json.bak")) {
            auto lib = decode(readFile(directory_ / L"stations.json.bak"));
            atomicWrite(path, encode(lib), false);
            warning_ = "Recovered station list from backup.";
            return lib;
        }
        return starterLibrary();
    }
    try {
        return decode(readFile(path));
    } catch (const std::exception&) {
        // Never replace damaged user data with defaults. Preserve it for recovery.
        auto previous = directory_ / L"stations.json.bak";
        if (!fs::exists(previous))
            throw std::runtime_error("The saved station file is damaged and no backup is available. Your "
                                     "file was left untouched.");
        auto lib = decode(readFile(previous));
        auto corrupt = directory_ / (L"stations.corrupt-" + wide(newId()) + L".json");
        fs::copy_file(path, corrupt);
        atomicWrite(path, encode(lib), false);
        warning_ = "Recovered station list from backup. The damaged file was preserved.";
        return lib;
    }
}
void Store::save(const Library& lib) {
    atomicWrite(directory_ / L"stations.json", encode(lib));
}
} // namespace radio
