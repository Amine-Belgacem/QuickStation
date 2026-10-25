#pragma once
#include <windows.h>
#include <commctrl.h>

namespace radio {
// An ordinary icon asset in a native BUTTON image list; Windows paints the control.
class ButtonIcon {
  public:
    ~ButtonIcon();
    ButtonIcon() = default;
    ButtonIcon(const ButtonIcon&) = delete;
    ButtonIcon& operator=(const ButtonIcon&) = delete;
    bool apply(HWND button, int resource, UINT dpi) noexcept;

  private:
    HIMAGELIST images_{};
    int resource_ = 0;
    UINT dpi_ = 0;
    void align(HWND button) noexcept;
};
} // namespace radio
