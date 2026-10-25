#include <windows.h>
#include <objbase.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include "app.h"
#include <stdexcept>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int show) {
    // Child audio processes inherit this before their DLL loader runs.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    const auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(com)) {
        MessageBoxW(nullptr, L"Cannot initialize Windows components.", L"QuickStation", MB_OK | MB_ICONERROR);
        return 1;
    }
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES |
                                                        ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES};
    int result = 1;
    HANDLE instanceLock = INVALID_HANDLE_VALUE;
    try {
        if (!InitCommonControlsEx(&controls))
            throw std::runtime_error("Cannot initialize Windows controls.");
        radio::fs::path data;
        int argc = 0;
        auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (!argv)
            throw std::runtime_error("Cannot read application arguments.");
        bool missingPath = false;
        for (int i = 1; i < argc; ++i) {
            if (std::wstring_view(argv[i]) == L"--data-dir") {
                if (i + 1 < argc)
                    data = argv[++i];
                else
                    missingPath = true;
            }
        }
        LocalFree(argv);
        if (missingPath)
            throw std::runtime_error("--data-dir requires a directory path.");
        if (data.empty()) {
            PWSTR path = nullptr;
            if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path)))
                throw std::runtime_error("Cannot locate local application data.");
            // Retain the existing profile location so upgrading never resets saved stations.
            data = radio::fs::path(path) / L"SmallRadio";
            CoTaskMemFree(path);
        }
        data = radio::fs::absolute(data);
        radio::fs::create_directories(data);
        instanceLock = CreateFileW((data / L"instance.lock").c_str(), GENERIC_READ | GENERIC_WRITE,
                                   FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (instanceLock == INVALID_HANDLE_VALUE)
            throw std::runtime_error(
                "QuickStation is already using this data folder, or the folder is not writable.");
        result = radio::runApplication(instance, show, data);
    } catch (const std::exception& error) {
        MessageBoxW(nullptr, radio::wide(error.what()).c_str(), L"QuickStation", MB_OK | MB_ICONERROR);
    }
    if (instanceLock != INVALID_HANDLE_VALUE)
        CloseHandle(instanceLock);
    CoUninitialize();
    return result;
}
