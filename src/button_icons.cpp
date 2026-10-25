#include "button_icons.h"
#include "ui_metrics.h"
#include <uxtheme.h>
#include <vsstyle.h>
#include <algorithm>
#include "../assets/resource.h"

namespace radio {
ButtonIcon::~ButtonIcon() {
    if (images_)
        ImageList_Destroy(images_);
}
bool ButtonIcon::apply(HWND button, int resource, UINT dpi) noexcept {
    const auto foreground = GetSysColor(COLOR_BTNTEXT);
    if (GetRValue(foreground) + GetGValue(foreground) + GetBValue(foreground) > 384)
        resource += IDI_BUTTON_WHITE_OFFSET;
    if (resource_ == resource && dpi_ == dpi && images_) {
        align(button);
        return true;
    }
    const auto instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(button, GWLP_HINSTANCE));
    const int size = MulDiv(16, dpi, 96);
    auto icon =
        static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(resource), IMAGE_ICON, size, size, 0));
    if (!icon)
        return false; // Optional icons never disable text-only playback controls.
    auto replacement = ImageList_Create(size, size, ILC_COLOR32 | ILC_MASK, 1, 0);
    const bool added = replacement && ImageList_AddIcon(replacement, icon) == 0;
    DestroyIcon(icon);
    if (!added) {
        if (replacement)
            ImageList_Destroy(replacement);
        return false;
    }
    BUTTON_IMAGELIST image{
        replacement,
        {MulDiv(8, dpi, 96), 0, MulDiv(UiSpacing::ButtonIconGap, dpi, 96), 0},
        BUTTON_IMAGELIST_ALIGN_LEFT};
    if (!Button_SetImageList(button, &image)) {
        ImageList_Destroy(replacement);
        return false;
    }
    if (images_)
        ImageList_Destroy(images_);
    images_ = replacement;
    resource_ = resource;
    dpi_ = dpi;
    align(button);
    return true;
}
void ButtonIcon::align(HWND button) noexcept {
    try {
        RECT content{};
        GetClientRect(button, &content);
        if (auto theme = OpenThemeData(button, L"Button")) {
            RECT themed{};
            if (SUCCEEDED(GetThemeBackgroundContentRect(theme, nullptr, BP_PUSHBUTTON, PBS_NORMAL, &content,
                                                        &themed)))
                content = themed;
            CloseThemeData(theme);
        } else {
            const int edge = GetSystemMetricsForDpi(SM_CXEDGE, dpi_);
            InflateRect(&content, -edge, -edge);
        }
        wchar_t label[32]{};
        GetWindowTextW(button, label, 32);
        const int resource =
            resource_ >= IDI_PLAY + IDI_BUTTON_WHITE_OFFSET ? resource_ - IDI_BUTTON_WHITE_OFFSET : resource_;
        // Optical bounds of the ordinary 16-DIP assets (see make-button-icons.ps1).
        const int left = MulDiv(resource == IDI_PLAY || resource == IDI_STOP ? 4 : 2, dpi_, 96);
        const int right = MulDiv(resource == IDI_STOP    ? 4
                                 : resource == IDI_PLAY  ? 3
                                 : resource == IDI_MUTED ? 1
                                                         : 0,
                                 dpi_, 96);
        const int size = MulDiv(16, dpi_, 96), gap = MulDiv(UiSpacing::ButtonIconGap, dpi_, 96);
        const int group = size - left - right + gap + nativeTextWidth(button, label);
        BUTTON_IMAGELIST image{
            images_,
            {std::max(0L, (content.right - content.left - group) / 2 - left), 0, std::max(0, gap - right), 0},
            BUTTON_IMAGELIST_ALIGN_LEFT};
        RECT margins{};
        Button_SetTextMargin(button, &margins);
        Button_SetImageList(button, &image);
    } catch (...) {
        // Font-metric failure must not disable an otherwise usable native button.
    }
}
} // namespace radio
