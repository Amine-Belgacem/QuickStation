#pragma once
#include <windows.h>
#include <commctrl.h>
#include <stdexcept>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <filesystem>

// Render only our own hidden production controls. This never reads desktop pixels.
class NativePixels {
  public:
    explicit NativePixels(HWND control, bool includeFrame = false) {
        RECT client{};
        if (includeFrame)
            GetWindowRect(control, &client);
        else
            GetClientRect(control, &client);
        width = client.right - client.left;
        height = client.bottom - client.top;
        if (width <= 0 || height <= 0 || width > 8192 || height > 8192)
            throw std::runtime_error("Invalid native rendering dimensions");
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        void* pixels{};
        bitmap_ = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        dc_ = CreateCompatibleDC(nullptr);
        if (!bitmap_ || !dc_) {
            if (bitmap_)
                DeleteObject(bitmap_);
            if (dc_)
                DeleteDC(dc_);
            throw std::runtime_error("Cannot render native control");
        }
        old_ = SelectObject(dc_, bitmap_);
        colors_ = static_cast<const DWORD*>(pixels);
        client = {0, 0, width, height};
        FillRect(dc_, &client, GetSysColorBrush(COLOR_WINDOW));
        SendMessageW(control, WM_PRINT, reinterpret_cast<WPARAM>(dc_),
                     PRF_CLIENT | PRF_CHILDREN | (includeFrame ? PRF_NONCLIENT : 0));
        GdiFlush();
    }
    ~NativePixels() {
        SelectObject(dc_, old_);
        DeleteDC(dc_);
        DeleteObject(bitmap_);
    }
    NativePixels(const NativePixels&) = delete;
    NativePixels& operator=(const NativePixels&) = delete;
    DWORD at(int x, int y) const {
        return colors_[std::clamp(y, 0, height - 1) * width + std::clamp(x, 0, width - 1)] & 0xffffff;
    }
    int width{}, height{};

  private:
    HBITMAP bitmap_{};
    HDC dc_{};
    HGDIOBJ old_{};
    const DWORD* colors_{};
};
inline bool nativeDarkPixel(DWORD color) {
    return (color & 255) < 80 && ((color >> 8) & 255) < 80 && ((color >> 16) & 255) < 80;
}
inline int nativeMarkerLogo(HIMAGELIST images, int size, int left, int top) {
    int width{}, height{};
    ImageList_GetIconSize(images, &width, &height);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void* pixels{};
    auto bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (!bitmap)
        throw std::runtime_error("Cannot create a native logo marker");
    auto* colors = static_cast<DWORD*>(pixels);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            colors[y * width + x] =
                x >= left && x < left + size && y >= top && y < top + size ? 0xffc020c0 : 0;
    const int result = ImageList_Add(images, bitmap, nullptr);
    DeleteObject(bitmap);
    if (result < 0)
        throw std::runtime_error("Cannot add a native logo marker");
    return result;
}
inline void saveNativeBitmap(const std::filesystem::path& path, const BITMAPINFOHEADER& info,
                             const void* pixels) {
    BITMAPFILEHEADER file{};
    file.bfType = 0x4d42;
    file.bfOffBits = sizeof(file) + sizeof(info);
    const auto bytes = info.biWidth * -info.biHeight * 4;
    file.bfSize = file.bfOffBits + bytes;
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(&file), sizeof(file));
    output.write(reinterpret_cast<const char*>(&info), sizeof(info));
    output.write(static_cast<const char*>(pixels), bytes);
}
