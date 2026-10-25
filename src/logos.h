#pragma once
#include "core.h"
#include <windows.h>
#include <commctrl.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <unordered_map>

namespace radio {
// Small thumbnails in a native image list. Windows handles drawing and selection.
class Logos {
  public:
    Logos() = default;
    ~Logos();
    Logos(const Logos&) = delete;
    Logos& operator=(const Logos&) = delete;
    void reset(int size, HICON fallback, int paddingX = 0, int paddingY = 0, int paddingRight = -1);
    int get(const std::string& url, const fs::path& path);
    HIMAGELIST images() const {
        return images_;
    }

  private:
    Microsoft::WRL::ComPtr<IWICImagingFactory> factory_;
    HIMAGELIST images_ = nullptr;
    int size_ = 40;
    int paddingX_ = 0, paddingY_ = 0, paddingRight_ = 0;
    std::unordered_map<std::string, int> loaded_;
};
} // namespace radio
