#pragma once
#include "core.h"
#include <windows.h>
#include <optional>

namespace radio {
// Keep Windows' workspace coordinates paired with its placement APIs.
std::optional<WINDOWPLACEMENT> loadWindowPlacement(const fs::path& path) noexcept;
void saveWindowPlacement(const fs::path& path, const WINDOWPLACEMENT& placement);
} // namespace radio
