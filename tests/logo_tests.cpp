#include <windows.h>
#include <objbase.h>
#include "logos.h"
#include <cstring>
#include <iostream>
#include <stdexcept>

using namespace radio;
namespace {
int checks = 0;
void check(bool condition, const char* message) {
    ++checks;
    if (!condition)
        throw std::runtime_error(message);
}
std::string bitmap(int width, int height) {
    BITMAPFILEHEADER file{};
    BITMAPINFOHEADER info{};
    file.bfType = 0x4d42;
    file.bfOffBits = sizeof(file) + sizeof(info);
    info.biSize = sizeof(info);
    info.biWidth = width;
    info.biHeight = height;
    info.biPlanes = 1;
    info.biBitCount = 32;
    info.biCompression = BI_RGB;
    const size_t pixels = static_cast<size_t>(width) * height * 4;
    file.bfSize = static_cast<DWORD>(file.bfOffBits + pixels);
    std::string data(file.bfSize, '\0');
    memcpy(data.data(), &file, sizeof(file));
    memcpy(data.data() + sizeof(file), &info, sizeof(info));
    std::fill(data.begin() + file.bfOffBits, data.end(), static_cast<char>(0xff));
    return data;
}
} // namespace
int main() {
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)))
        return 1;
    const auto root = fs::current_path() / "test-data" / ("logo-tests-" + newId());
    try {
        fs::create_directories(root);
        atomicWrite(root / "small.bmp", bitmap(2, 2), false);
        atomicWrite(root / "large.bmp", bitmap(2049, 1), false);
        atomicWrite(root / "bad.img", "broken image", false);
        atomicWrite(root / "oversized.bmp", bitmap(2, 2) + std::string(1024 * 1024, 'x'), false);
        {
            Logos logos;
            const auto fallback = LoadIconW(nullptr, IDI_APPLICATION);
            logos.reset(40, fallback);
            check(ImageList_GetImageCount(logos.images()) == 1, "Fallback icon available");
            check(logos.get("https://test/missing", root / "missing") == 0, "Missing logo uses fallback");
            const auto image = logos.get("https://test/valid", root / "small.bmp");
            check(image > 0, "Windows decoder loads a real bitmap");
            check(logos.get("https://test/valid", root / "small.bmp") == image, "Decoded logo reused");
            check(logos.get("https://test/bad", root / "bad.img") == 0, "Invalid image uses fallback");
            atomicWrite(root / "bad.img", bitmap(2, 2), false);
            check(logos.get("https://test/bad", root / "bad.img") > 0,
                  "Replacing corrupt cache refreshes a previously missing logo");
            check(logos.get("https://test/large", root / "large.bmp") == 0, "Oversized dimensions rejected");
            check(logos.get("https://test/oversized", root / "oversized.bmp") == 0,
                  "Oversized on-disk images are rejected before UI-thread decoding");
            for (int i = 0; i < 300; ++i)
                logos.get("https://test/" + std::to_string(i), root / "small.bmp");
            check(ImageList_GetImageCount(logos.images()) <= 257, "Thumbnail memory bounded");
            logos.reset(32, fallback, 8, 12);
            int cellWidth = 0, cellHeight = 0;
            ImageList_GetIconSize(logos.images(), &cellWidth, &cellHeight);
            check(cellWidth == 48 && cellHeight == 56 &&
                      logos.get("https://test/padded", root / "small.bmp") > 0,
                  "Padded native image cells provide row spacing");
            // Windows caches resources for each icon size on first use. Compare steady-state
            // usage after both sizes have been exercised, so the test detects continuing leaks.
            for (int size : {80, 40}) {
                logos.reset(size, fallback);
                logos.get("https://test/valid", root / "small.bmp");
            }
            const auto handles = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
            for (int i = 0; i < 20; ++i) {
                logos.reset(i % 2 ? 40 : 80, fallback);
                logos.get("https://test/valid", root / "small.bmp");
            }
            check(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) <= handles + 2,
                  "DPI resets release previous graphics handles");
        }
        fs::remove_all(root);
        std::cout << "PASS: " << checks << " Windows image checks\n";
        CoUninitialize();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        CoUninitialize();
        return 1;
    }
}
