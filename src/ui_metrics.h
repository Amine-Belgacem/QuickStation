#pragma once
#include <windows.h>
#include <string>

namespace radio {
std::wstring controlText(HWND control);
int nativeTextHeight(HWND window);
int nativeTextWidth(HWND window, const wchar_t* text);
struct UiSpacing {
    static constexpr int Margin = 12, Gap = 8, Section = 16, LabelGap = 4;
    static constexpr int Input = 32, Button = 32, ButtonWidth = 88, Logo = 32, PlayingLogo = 44;
    static constexpr int RowPadding = 3, ButtonIconGap = 9;
    // Native report views place image cells 2 physical pixels before subitem text.
    static constexpr int ListImageInset = 2;
};
int nativeFieldHeight(HWND window, UINT dpi);
// A native themed EDIT border surrounds the original single-line input. No custom painting.
void attachCenteredEdit(HWND edit);
void placeCenteredEdit(HWND edit, int x, int y, int width, int height);
HWND nativeEditFrame(HWND edit);
HBRUSH nativeFieldBrush(HWND frame, HDC dc);
void placeControl(HWND control, int x, int y, int width, int height);
void centerDialogOnOwner(HWND dialog);
void keepListFocusVisible(HWND list);
} // namespace radio
