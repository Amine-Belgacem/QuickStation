#pragma once
#include <windows.h>
#include "core.h"

namespace radio {
int runApplication(HINSTANCE instance, int show, const fs::path& data);
#ifdef QUICKSTATION_UI_TESTS
int runNativeUiChecks(HINSTANCE instance, const fs::path& data);
#endif
} // namespace radio
