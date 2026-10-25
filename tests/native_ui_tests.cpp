#include <windows.h>
#include <commctrl.h>
#include <objbase.h>
#include "app.h"
#include <iostream>

int main() {
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)))
        return 1;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_WIN95_CLASSES | ICC_BAR_CLASSES};
    if (!InitCommonControlsEx(&controls))
        return 1;
    const auto root = radio::fs::current_path() / "test-data" / ("native-ui-tests-" + radio::newId());
    try {
        radio::Library library;
        library.muted = true;
        library.stations = {{radio::newId(), "Favorite one", "http://127.0.0.1:1/one", "", "Test", true},
                            {radio::newId(), "Favorite two", "http://127.0.0.1:1/two", "", "Test", true},
                            {radio::newId(), "Other station", "http://127.0.0.1:1/other", "", "Test", false}};
        {
            radio::Store store(root);
            store.save(library);
        }
        const int checks = radio::runNativeUiChecks(GetModuleHandleW(nullptr), root);
        radio::fs::remove_all(root);
        std::cout << "PASS: " << checks << " native UI checks\n";
        CoUninitialize();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        CoUninitialize();
        return 1;
    }
}
