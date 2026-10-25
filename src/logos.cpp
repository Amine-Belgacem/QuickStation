#include "logos.h"
#include <stdexcept>
#include <vector>
#include <cstring>

namespace radio {
Logos::~Logos() {
    if (images_)
        ImageList_Destroy(images_);
}
void Logos::reset(int size, HICON fallback, int paddingX, int paddingY, int paddingRight) {
    if (paddingRight == -1)
        paddingRight = paddingX;
    if (size <= 0 || size > 256 || paddingX < 0 || paddingY < 0 || paddingX > 128 || paddingY > 128 ||
        paddingRight < 0 || paddingRight > 128)
        throw std::runtime_error("Invalid image-list dimensions.");
    const int width = size + paddingX + paddingRight, height = size + 2 * paddingY;
    auto replacement = ImageList_Create(width, height, ILC_COLOR32, 16, 16);
    if (!replacement)
        throw std::runtime_error("Cannot create station image list.");
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void* pixels = nullptr;
    auto bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    auto dc = CreateCompatibleDC(nullptr);
    int added = -1;
    if (bitmap && dc) {
        memset(pixels, 0, static_cast<size_t>(width) * height * 4);
        auto old = SelectObject(dc, bitmap);
        DrawIconEx(dc, paddingX, paddingY, fallback, size, size, 0, nullptr, DI_NORMAL);
        SelectObject(dc, old);
        added = ImageList_Add(replacement, bitmap, nullptr);
    }
    if (dc)
        DeleteDC(dc);
    if (bitmap)
        DeleteObject(bitmap);
    if (added < 0) {
        ImageList_Destroy(replacement);
        throw std::runtime_error("Cannot create the default station icon.");
    }
    if (images_)
        ImageList_Destroy(images_);
    images_ = replacement;
    size_ = size;
    paddingX_ = paddingX;
    paddingY_ = paddingY;
    paddingRight_ = paddingRight;
    loaded_.clear();
    if (!factory_) {
        // Images are optional; a decoder failure falls back to the application icon.
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                         IID_PPV_ARGS(factory_.GetAddressOf()));
    }
}
int Logos::get(const std::string& url, const fs::path& path) {
    if (url.empty() || !factory_)
        return 0;
    if (auto found = loaded_.find(url); found != loaded_.end())
        return found->second;
    if (loaded_.size() >= 256)
        return 0; // Bound decoded image memory independently of station count.
    std::error_code ec;
    const auto fileBytes = fs::file_size(path, ec);
    if (ec || fileBytes > 1024 * 1024)
        return 0; // Missing files may arrive later from the network worker.
    int index = 0;
    Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
    Microsoft::WRL::ComPtr<IWICBitmapScaler> scaler;
    Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
    UINT sourceWidth = 0, sourceHeight = 0;
    if (SUCCEEDED(factory_->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                      WICDecodeMetadataCacheOnDemand, &decoder)) &&
        SUCCEEDED(decoder->GetFrame(0, &frame)) && SUCCEEDED(frame->GetSize(&sourceWidth, &sourceHeight)) &&
        sourceWidth && sourceHeight && sourceWidth <= 2048 && sourceHeight <= 2048 &&
        SUCCEEDED(factory_->CreateBitmapScaler(&scaler)) &&
        SUCCEEDED(scaler->Initialize(frame.Get(), size_, size_, WICBitmapInterpolationModeFant)) &&
        SUCCEEDED(factory_->CreateFormatConverter(&converter)) &&
        SUCCEEDED(converter->Initialize(scaler.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
                                        nullptr, 0, WICBitmapPaletteTypeCustom))) {
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        const int width = size_ + paddingX_ + paddingRight_, height = size_ + 2 * paddingY_;
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        // Allocate before acquiring the bitmap so allocation failure cannot leak GDI resources.
        std::vector<BYTE> decoded(static_cast<size_t>(size_) * size_ * 4);
        void* pixels = nullptr;
        HBITMAP bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        if (bitmap) {
            const auto stride = static_cast<UINT>(width * 4);
            memset(pixels, 0, static_cast<size_t>(stride) * height);
            if (SUCCEEDED(converter->CopyPixels(nullptr, size_ * 4, static_cast<UINT>(decoded.size()),
                                                decoded.data()))) {
                for (int y = 0; y < size_; ++y)
                    memcpy(static_cast<BYTE*>(pixels) + (y + paddingY_) * stride + paddingX_ * 4,
                           decoded.data() + static_cast<size_t>(y) * size_ * 4, size_ * 4);
                auto added = ImageList_Add(images_, bitmap, nullptr);
                if (added >= 0)
                    index = added;
            }
            DeleteObject(bitmap);
        }
    }
    // A failed decode must not pin the fallback after the worker replaces a bad cache file.
    // The worker validates/removes invalid files and bounds its own retry attempts.
    if (index > 0)
        loaded_.emplace(url, index);
    return index;
}
} // namespace radio
