#pragma once
#include <windows.h>
#include "network.h"
#include <functional>

namespace radio {
using AddFavorite = std::function<void(const Station&)>;
void browseStations(HINSTANCE instance, HWND owner, Network& network, AddFavorite add);
#ifdef QUICKSTATION_UI_TESTS
int runBrowseUiChecks(HINSTANCE instance, HWND owner, const fs::path& data);
#endif
} // namespace radio
