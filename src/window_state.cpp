#include "window_state.h"
#include <json.hpp>
#include <stdexcept>

namespace radio {
namespace {
bool validRect(const RECT& rect) {
    for (const auto coordinate : {rect.left, rect.top, rect.right, rect.bottom})
        if (coordinate < -100000 || coordinate > 100000)
            return false;
    const auto width = rect.right - rect.left, height = rect.bottom - rect.top;
    return width >= 100 && width <= 32768 && height >= 100 && height <= 32768;
}
} // namespace
std::optional<WINDOWPLACEMENT> loadWindowPlacement(const fs::path& path) noexcept {
    try {
        const auto json = nlohmann::json::parse(
            readFile(path, 2048), [](int depth, nlohmann::json::parse_event_t, nlohmann::json&) {
                if (depth > 4)
                    throw std::runtime_error("Window settings nesting is too deep");
                return true;
            });
        if (!json.is_object() || json.at("version") != 1)
            return {};
        const auto& normal = json.at("normal");
        if (!normal.is_array() || normal.size() != 4 || !json.at("maximized").is_boolean())
            return {};
        LONG coordinates[4]{};
        for (size_t i = 0; i < 4; ++i) {
            if (!normal[i].is_number_integer() ||
                (normal[i].is_number_unsigned() && normal[i].get<uint64_t>() > 100000))
                return {};
            const auto value = normal[i].get<int64_t>();
            if (value < -100000 || value > 100000)
                return {};
            coordinates[i] = static_cast<LONG>(value);
        }
        WINDOWPLACEMENT placement{sizeof(placement)};
        placement.rcNormalPosition = {coordinates[0], coordinates[1], coordinates[2], coordinates[3]};
        if (!validRect(placement.rcNormalPosition))
            return {};
        placement.ptMinPosition = {-1, -1};
        placement.ptMaxPosition = {-1, -1};
        placement.showCmd = json.at("maximized").get<bool>() ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
        return placement;
    } catch (...) {
        // Bad/missing optional window settings must never stop station playback or startup.
        return {};
    }
}
void saveWindowPlacement(const fs::path& path, const WINDOWPLACEMENT& placement) {
    if (!validRect(placement.rcNormalPosition))
        throw std::runtime_error("Cannot save invalid window bounds.");
    const bool maximized =
        placement.showCmd == SW_SHOWMAXIMIZED ||
        (placement.showCmd == SW_SHOWMINIMIZED && (placement.flags & WPF_RESTORETOMAXIMIZED));
    const auto& rect = placement.rcNormalPosition;
    atomicWrite(path,
                nlohmann::json{{"version", 1},
                               {"normal", {rect.left, rect.top, rect.right, rect.bottom}},
                               {"maximized", maximized}}
                    .dump(2),
                false);
}
} // namespace radio
