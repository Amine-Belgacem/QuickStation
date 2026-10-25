#include "ui_metrics.h"
#include <algorithm>
#include <stdexcept>
#include <commctrl.h>
#include <windowsx.h>
#include <memory>

namespace radio {
std::wstring controlText(HWND control) {
    const int length = GetWindowTextLengthW(control);
    std::wstring value(static_cast<size_t>(length) + 1, L'\0');
    value.resize(GetWindowTextW(control, value.data(), length + 1));
    return value;
}
int nativeTextWidth(HWND window, const wchar_t* text) {
    auto dc = GetDC(window);
    if (!dc)
        throw std::runtime_error("Cannot read control text size.");
    auto font = reinterpret_cast<HFONT>(SendMessageW(window, WM_GETFONT, 0, 0));
    auto old = SelectObject(dc, font ? font : GetStockObject(DEFAULT_GUI_FONT));
    SIZE size{};
    const bool ok = GetTextExtentPoint32W(dc, text, lstrlenW(text), &size) != FALSE;
    SelectObject(dc, old);
    ReleaseDC(window, dc);
    if (!ok)
        throw std::runtime_error("Cannot read control text size.");
    return size.cx;
}
int nativeTextHeight(HWND window) {
    HDC dc = GetDC(window);
    if (!dc)
        throw std::runtime_error("Cannot read control font metrics.");
    auto font = reinterpret_cast<HFONT>(SendMessageW(window, WM_GETFONT, 0, 0));
    auto old = SelectObject(dc, font ? font : GetStockObject(DEFAULT_GUI_FONT));
    TEXTMETRICW metrics{};
    const bool ok = GetTextMetricsW(dc, &metrics) != FALSE;
    SelectObject(dc, old);
    ReleaseDC(window, dc);
    if (!ok)
        throw std::runtime_error("Cannot read control font metrics.");
    return metrics.tmHeight;
}
int nativeFieldHeight(HWND window, UINT dpi) {
    return std::max(MulDiv(UiSpacing::Input, dpi, 96),
                    nativeTextHeight(window) + MulDiv(2 * UiSpacing::Gap, dpi, 96));
}
void placeControl(HWND control, int x, int y, int width, int height) {
    SetWindowPos(control, nullptr, x, y, std::max(width, 0), std::max(height, 0),
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW);
}
namespace {
constexpr UINT_PTR FocusSubclass = 2;
LRESULT CALLBACK listFocusProc(HWND window, UINT message, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    if (message == WM_UPDATEUISTATE && (HIWORD(wp) & UISF_HIDEFOCUS)) {
        // Keep Windows' own focus rectangle visible for mouse and keyboard selection alike.
        const auto result =
            DefSubclassProc(window, message, MAKEWPARAM(LOWORD(wp), HIWORD(wp) & ~UISF_HIDEFOCUS), lp);
        DefSubclassProc(window, message, MAKEWPARAM(UIS_CLEAR, UISF_HIDEFOCUS), lp);
        return result;
    }
    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(window, listFocusProc, FocusSubclass);
    }
    return DefSubclassProc(window, message, wp, lp);
}
constexpr UINT_PTR EditSubclass = 1, FrameSubclass = 1;
constexpr wchar_t FrameProperty[] = L"QuickStation.NativeEditFrame";
struct Field {
    HWND edit{}, frame{};
};
void alignField(Field& field) {
    if (!IsWindow(field.frame))
        return;
    RECT client{};
    GetClientRect(field.frame, &client);
    POINT origin{};
    MapWindowPoints(field.frame, GetParent(field.edit), &origin, 1);
    const auto dpi = GetDpiForWindow(field.edit);
    const int inset = MulDiv(UiSpacing::LabelGap, dpi, 96);
    const int line =
        std::min(static_cast<int>(client.bottom), nativeTextHeight(field.edit) + 2 * MulDiv(1, dpi, 96));
    SetWindowPos(field.edit, nullptr, origin.x + inset, origin.y + (client.bottom - line) / 2,
                 std::max(0, static_cast<int>(client.right) - 2 * inset), line,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW);
}
LRESULT CALLBACK frameProc(HWND window, UINT message, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data) {
    auto& field = *reinterpret_cast<Field*>(data);
    if (message == WM_GETDLGCODE)
        return 0; // The inner edit is the only keyboard input.
    if (message == WM_SETFOCUS) {
        SetFocus(field.edit);
        return 0;
    }
    if (message == WM_LBUTTONDOWN && IsWindow(field.edit)) {
        SetFocus(field.edit);
        POINT point{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        MapWindowPoints(window, field.edit, &point, 1);
        RECT r{};
        GetClientRect(field.edit, &r);
        point.y = std::clamp(point.y, 0L, std::max(0L, r.bottom - 1));
        SendMessageW(field.edit, WM_LBUTTONDOWN, wp, MAKELPARAM(point.x, point.y));
        SendMessageW(field.edit, WM_LBUTTONUP, 0, MAKELPARAM(point.x, point.y));
        return 0;
    }
    if (message == WM_NCDESTROY) {
        RemovePropW(window, FrameProperty);
        RemoveWindowSubclass(window, frameProc, FrameSubclass);
        field.frame = nullptr;
    }
    return DefSubclassProc(window, message, wp, lp);
}
LRESULT CALLBACK editProc(HWND window, UINT message, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data) {
    auto* field = reinterpret_cast<Field*>(data);
    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(window, editProc, EditSubclass);
        if (IsWindow(field->frame))
            DestroyWindow(field->frame);
        const auto result = DefSubclassProc(window, message, wp, lp);
        delete field;
        return result;
    }
    const auto result = DefSubclassProc(window, message, wp, lp);
    if (message == WM_SHOWWINDOW && IsWindow(field->frame))
        ShowWindow(field->frame, wp ? SW_SHOWNA : SW_HIDE);
    if (message == WM_ENABLE && IsWindow(field->frame))
        EnableWindow(field->frame, wp != 0);
    if (message == WM_SETFONT || message == WM_DPICHANGED_AFTERPARENT) {
        // Windows callbacks never propagate C++ exceptions across the message boundary.
        try {
            alignField(*field);
        } catch (const std::exception&) {
        }
    }
    return result;
}
Field* fieldFor(HWND edit) {
    DWORD_PTR data{};
    return GetWindowSubclass(edit, editProc, EditSubclass, &data) ? reinterpret_cast<Field*>(data) : nullptr;
}
} // namespace
void keepListFocusVisible(HWND list) {
    if (!SetWindowSubclass(list, listFocusProc, FocusSubclass, 0))
        throw std::runtime_error("Cannot initialize native list focus handling.");
    SendMessageW(list, WM_UPDATEUISTATE, MAKEWPARAM(UIS_CLEAR, UISF_HIDEFOCUS), 0);
}
void attachCenteredEdit(HWND edit) {
    if (fieldFor(edit))
        return;
    auto field = std::make_unique<Field>();
    field->edit = edit;
    RECT r{};
    GetWindowRect(edit, &r);
    MapWindowPoints(nullptr, GetParent(edit), reinterpret_cast<POINT*>(&r), 2);
    field->frame = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_CLIPSIBLINGS | ES_READONLY | (GetWindowLongW(edit, GWL_STYLE) & WS_VISIBLE), r.left,
        r.top, r.right - r.left, r.bottom - r.top, GetParent(edit), nullptr,
        reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(edit, GWLP_HINSTANCE)), nullptr);
    if (!field->frame)
        throw std::runtime_error("Cannot create the native input frame.");
    if (!SetPropW(field->frame, FrameProperty, edit) ||
        !SetWindowSubclass(field->frame, frameProc, FrameSubclass,
                           reinterpret_cast<DWORD_PTR>(field.get())) ||
        !SetWindowSubclass(edit, editProc, EditSubclass, reinterpret_cast<DWORD_PTR>(field.get()))) {
        DestroyWindow(field->frame);
        throw std::runtime_error("Cannot initialize the native input frame.");
    }
    // Keep the edit's original tab order; only its non-focusable frame is inserted behind it.
    SetWindowPos(field->frame, edit, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOREDRAW);
    SetWindowLongPtrW(edit, GWL_STYLE, GetWindowLongPtrW(edit, GWL_STYLE) & ~WS_BORDER);
    SetWindowLongPtrW(edit, GWL_EXSTYLE, GetWindowLongPtrW(edit, GWL_EXSTYLE) & ~WS_EX_CLIENTEDGE);
    SetWindowPos(edit, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    auto* owned = field.release();
    alignField(*owned);
}
void placeCenteredEdit(HWND edit, int x, int y, int width, int height) {
    auto* field = fieldFor(edit);
    if (!field)
        throw std::runtime_error("Native input frame is not initialized.");
    placeControl(field->frame, x, y, width, height);
    alignField(*field);
}
HWND nativeEditFrame(HWND edit) {
    const auto* field = fieldFor(edit);
    return field ? field->frame : nullptr;
}
HBRUSH nativeFieldBrush(HWND frame, HDC dc) {
    const auto edit = static_cast<HWND>(GetPropW(frame, FrameProperty));
    if (!edit)
        return nullptr;
    const auto color = IsWindowEnabled(edit) ? COLOR_WINDOW : COLOR_BTNFACE;
    SetBkColor(dc, GetSysColor(color));
    return GetSysColorBrush(color);
}
void centerDialogOnOwner(HWND dialog) {
    RECT owner{}, r{};
    GetWindowRect(GetParent(dialog), &owner);
    GetWindowRect(dialog, &r);
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromWindow(GetParent(dialog), MONITOR_DEFAULTTONEAREST), &monitor);
    const int width = r.right - r.left, height = r.bottom - r.top;
    const auto& work = monitor.rcWork;
    const int x = std::clamp(owner.left + (owner.right - owner.left - width) / 2, work.left,
                             std::max(work.left, work.right - width));
    const int y = std::clamp(owner.top + (owner.bottom - owner.top - height) / 2, work.top,
                             std::max(work.top, work.bottom - height));
    SetWindowPos(dialog, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}
} // namespace radio
