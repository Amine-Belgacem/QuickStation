#include <windows.h>
#include <objbase.h>
#include "app.h"
#include "audio.h"
#include "network.h"
#include "logos.h"
#include "ui_metrics.h"
#include "button_icons.h"
#include "window_state.h"
#include "browse_dialog.h"
#include "../assets/resource.h"
#include <commctrl.h>
#include <commdlg.h>
#include <uxtheme.h>
#include <vsstyle.h>
#include <windowsx.h>
#include <cwctype>
#include <optional>
#include <stdexcept>
#ifdef QUICKSTATION_UI_TESTS
#include <iostream>
#include "../tests/native_render.h"
#endif

namespace radio {
namespace {
enum : int {
    Tabs = 200,
    Find,
    Stations,
    Play,
    Mute,
    Volume,
    Filter,
    Hint,
    PlayingGroup,
    PlayingIcon,
    PlayingName,
    PlayingTitle,
    Status,
    Level = 215,
    Add = 300,
    Edit,
    Remove,
    Import,
    Export,
    DoubleClick,
    Usage,
    About,
    ToggleFavorite,
    MoveUp,
    MoveDown
};
constexpr UINT ApplyFavorites = WM_APP + 1;
constexpr UINT UiTimer = 1;
constexpr UINT FilterTimer = 2;
constexpr UINT_PTR VolumeClickSubclass = 1;
constexpr int ConnectionPart = 1, SavePart = 3;
using Clock = std::chrono::steady_clock;
LRESULT CALLBACK volumeClickProc(HWND slider, UINT message, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    if (message == WM_NCDESTROY)
        RemoveWindowSubclass(slider, volumeClickProc, VolumeClickSubclass);
    if (message == WM_LBUTTONDOWN) {
        const POINT click{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        RECT thumb{};
        SendMessageW(slider, TBM_GETTHUMBRECT, 0, reinterpret_cast<LPARAM>(&thumb));
        if (!PtInRect(&thumb, click)) {
            RECT channel{};
            SendMessageW(slider, TBM_GETCHANNELRECT, 0, reinterpret_cast<LPARAM>(&channel));
            const int minimum = static_cast<int>(SendMessageW(slider, TBM_GETRANGEMIN, 0, 0));
            const int maximum = static_cast<int>(SendMessageW(slider, TBM_GETRANGEMAX, 0, 0));
            const int halfThumb = (thumb.right - thumb.left) / 2;
            const int travelLeft = channel.left + halfThumb;
            const int travelRight = channel.right - halfThumb;
            if (travelRight > travelLeft && maximum > minimum) {
                const int x = std::clamp(static_cast<int>(click.x), travelLeft, travelRight);
                const int position = minimum + MulDiv(x - travelLeft, maximum - minimum,
                                                       travelRight - travelLeft);
                SetFocus(slider);
                SendMessageW(slider, TBM_SETPOS, TRUE, position);
                SendMessageW(GetParent(slider), WM_HSCROLL, MAKEWPARAM(TB_THUMBPOSITION, position),
                             reinterpret_cast<LPARAM>(slider));
                // Let the native control start a normal thumb drag if the new
                // thumb now covers the pointer.
                SendMessageW(slider, TBM_GETTHUMBRECT, 0, reinterpret_cast<LPARAM>(&thumb));
                if (!PtInRect(&thumb, click))
                    return 0;
            }
        }
    }
    return DefSubclassProc(slider, message, wp, lp);
}
void setText(HWND control, const std::wstring& value) {
    if (!control)
        return;
    const int length = GetWindowTextLengthW(control);
    std::wstring old(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(control, old.data(), length + 1);
    old.resize(length);
    if (old != value)
        SetWindowTextW(control, value.c_str());
}
void drawAudioLevel(const DRAWITEMSTRUCT& item, uint16_t level, UINT dpi) {
    RECT inside = item.rcItem;
    bool themed = false;
    if (HTHEME theme = OpenThemeData(item.hwndItem, L"Button")) {
        if (SUCCEEDED(DrawThemeBackground(theme, item.hDC, BP_PUSHBUTTON, PBS_NORMAL,
                                          &item.rcItem, nullptr))) {
            RECT content{};
            if (SUCCEEDED(GetThemeBackgroundContentRect(theme, item.hDC, BP_PUSHBUTTON,
                                                        PBS_NORMAL, &item.rcItem, &content)))
                inside = content;
            themed = true;
        }
        CloseThemeData(theme);
    }
    if (!themed) {
        DrawFrameControl(item.hDC, &inside, DFC_BUTTON, DFCS_BUTTONPUSH);
        const int edgeX = GetSystemMetricsForDpi(SM_CXEDGE, dpi);
        const int edgeY = GetSystemMetricsForDpi(SM_CYEDGE, dpi);
        InflateRect(&inside, -edgeX, -edgeY);
    }
    auto px = [&](int value) { return MulDiv(value, dpi, 96); };
    const int barWidth = std::max(2, px(3)), gap = std::max(1, px(2));
    const int totalWidth = 5 * barWidth + 4 * gap;
    const int x = inside.left + (inside.right - inside.left - totalWidth) / 2;
    constexpr uint16_t thresholds[] = {512, 2048, 4096, 8192, 16384};
    for (int i = 0; i < 5; ++i) {
        const int height = std::max(0, std::min<int>(px(6 + 4 * i),
                                                      inside.bottom - inside.top - px(3)));
        const int bottom = inside.bottom - px(2);
        RECT bar{x + i * (barWidth + gap), bottom - height,
                 x + i * (barWidth + gap) + barWidth, bottom};
        FillRect(item.hDC, &bar,
                 GetSysColorBrush(level >= thresholds[i] ? COLOR_HIGHLIGHT : COLOR_3DLIGHT));
    }
}
void errorBox(HWND hwnd, const std::exception& error) {
    MessageBoxW(hwnd, wide(error.what()).c_str(), L"QuickStation", MB_OK | MB_ICONERROR);
}
constexpr UINT RelayoutEditor = WM_APP + 10;
void layoutEditor(HWND dlg, bool initial = false) {
    const auto dpi = GetDpiForWindow(dlg);
    auto px = [&](int value) { return MulDiv(value, dpi, 96); };
    const int margin = px(UiSpacing::Margin), section = px(UiSpacing::Section);
    const int line = nativeTextHeight(GetDlgItem(dlg, IDC_NAME)),
              field = nativeFieldHeight(GetDlgItem(dlg, IDC_NAME), dpi);
    const int checkbox = std::max(line, px(20)), button = px(UiSpacing::Button);
    if (initial) {
        RECT r{0, 0, px(520),
               2 * margin + 4 * (line + px(UiSpacing::LabelGap) + field + section) + checkbox + section +
                   button};
        AdjustWindowRectExForDpi(&r, static_cast<DWORD>(GetWindowLongPtrW(dlg, GWL_STYLE)), FALSE,
                                 static_cast<DWORD>(GetWindowLongPtrW(dlg, GWL_EXSTYLE)), dpi);
        SetWindowPos(dlg, nullptr, 0, 0, r.right - r.left, r.bottom - r.top,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        centerDialogOnOwner(dlg);
    }
    RECT r{};
    GetClientRect(dlg, &r);
    int y = margin;
    for (auto [id, label] : {std::pair{IDC_NAME, IDC_NAME_LABEL}, std::pair{IDC_URL, IDC_URL_LABEL},
                             std::pair{IDC_LOGO, IDC_LOGO_LABEL}, std::pair{IDC_GENRE, IDC_GENRE_LABEL}}) {
        placeControl(GetDlgItem(dlg, label), margin, y, r.right - 2 * margin, line);
        y += line + px(UiSpacing::LabelGap);
        placeCenteredEdit(GetDlgItem(dlg, id), margin, y, r.right - 2 * margin, field);
        y += field + section;
    }
    placeControl(GetDlgItem(dlg, IDC_FAVORITE), margin, y, r.right - 2 * margin, checkbox);
    const int x = r.right - margin - px(UiSpacing::ButtonWidth);
    placeControl(GetDlgItem(dlg, IDCANCEL), x, r.bottom - margin - button, px(UiSpacing::ButtonWidth),
                 button);
    placeControl(GetDlgItem(dlg, IDOK), x - px(UiSpacing::Gap) - px(UiSpacing::ButtonWidth),
                 r.bottom - margin - button, px(UiSpacing::ButtonWidth), button);
    RedrawWindow(dlg, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}
std::optional<fs::path> chooseFile(HWND hwnd, bool save) {
    wchar_t file[32768]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"Radio station list (*.json)\0*.json\0M3U playlist (*.m3u;*.m3u8)\0*.m3u;*.m3u8\0\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = 32768;
    ofn.lpstrDefExt = L"json";
    ofn.Flags =
        OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    if (save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn))
        return fs::path(file);
    if (CommDlgExtendedError())
        throw std::runtime_error("Could not open file picker.");
    return {};
}
struct Editor {
    Station station;
};
INT_PTR editMessage(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto* edit = reinterpret_cast<Editor*>(GetWindowLongPtrW(dlg, DWLP_USER));
    if (msg == WM_INITDIALOG) {
        edit = reinterpret_cast<Editor*>(lp);
        SetWindowLongPtrW(dlg, DWLP_USER, lp);
        for (auto [id, value] :
             {std::pair{IDC_NAME, edit->station.name}, std::pair{IDC_URL, edit->station.url},
              std::pair{IDC_LOGO, edit->station.logo}, std::pair{IDC_GENRE, edit->station.genre}}) {
            SetDlgItemTextW(dlg, id, wide(value).c_str());
            SendDlgItemMessageW(dlg, id, EM_SETLIMITTEXT, (id == IDC_URL || id == IDC_LOGO) ? 4096 : 256, 0);
            attachCenteredEdit(GetDlgItem(dlg, id));
        }
        CheckDlgButton(dlg, IDC_FAVORITE, edit->station.favorite ? BST_CHECKED : BST_UNCHECKED);
        layoutEditor(dlg, true);
        return TRUE;
    }
    if (msg == WM_CTLCOLORSTATIC || msg == WM_CTLCOLOREDIT)
        if (auto brush = nativeFieldBrush(reinterpret_cast<HWND>(lp), reinterpret_cast<HDC>(wp)))
            return reinterpret_cast<INT_PTR>(brush);
    if (msg == RelayoutEditor) {
        layoutEditor(dlg);
        return TRUE;
    }
    if (msg == WM_DPICHANGED) {
        PostMessageW(dlg, RelayoutEditor, 0, 0);
        return FALSE;
    }
    if (msg == WM_COMMAND) {
        if (LOWORD(wp) == IDCANCEL) {
            EndDialog(dlg, 0);
            return TRUE;
        }
        if (LOWORD(wp) == IDOK) {
            try {
                auto s = edit->station;
                s.name = trim(utf8(controlText(GetDlgItem(dlg, IDC_NAME))));
                s.url = trim(utf8(controlText(GetDlgItem(dlg, IDC_URL))));
                s.logo = trim(utf8(controlText(GetDlgItem(dlg, IDC_LOGO))));
                s.genre = trim(utf8(controlText(GetDlgItem(dlg, IDC_GENRE))));
                s.favorite = IsDlgButtonChecked(dlg, IDC_FAVORITE) == BST_CHECKED;
                validate(s);
                if (s.id.empty())
                    s.id = newId();
                edit->station = std::move(s);
                EndDialog(dlg, 1);
            } catch (const std::exception& e) {
                errorBox(dlg, e);
            }
            return TRUE;
        }
    }
    return FALSE;
}
INT_PTR CALLBACK editProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    try {
        return editMessage(dlg, msg, wp, lp);
    } catch (const std::exception& error) {
        errorBox(dlg, error);
        if (msg == WM_INITDIALOG)
            EndDialog(dlg, 0);
        return TRUE;
    }
}
// Menu handles own their submenus once appended and are released to Windows
// only after SetMenu succeeds. Allocation failures cannot leak partial menus.
class NativeMenu {
  public:
    explicit NativeMenu(bool popup = true) : handle_(popup ? CreatePopupMenu() : CreateMenu()) {
        if (!handle_)
            throw std::runtime_error("Cannot create a native menu.");
    }
    ~NativeMenu() {
        if (handle_)
            DestroyMenu(handle_);
    }
    NativeMenu(const NativeMenu&) = delete;
    NativeMenu& operator=(const NativeMenu&) = delete;
    HMENU get() const {
        return handle_;
    }
    HMENU release() {
        const auto result = handle_;
        handle_ = nullptr;
        return result;
    }
    void append(UINT flags, UINT_PTR command, const wchar_t* caption) {
        if (!AppendMenuW(handle_, flags, command, caption))
            throw std::runtime_error("Cannot create a menu item.");
    }
    void submenu(NativeMenu& menu, const wchar_t* caption) {
        append(MF_POPUP, reinterpret_cast<UINT_PTR>(menu.get()), caption);
        menu.release();
    }

  private:
    HMENU handle_{};
};
} // namespace

class App {
  public:
    explicit App(fs::path data)
        : store_(std::move(data)), library_(store_.load()),
          audio_(false, store_.directory() / L"playback.log"), network_(store_.directory() / L"logos") {
        if (!fs::exists(store_.directory() / L"stations.json"))
            store_.save(library_);
        audio_.setVolume(library_.volume, library_.muted);
    }
    ~App() {
        if (hwnd_ && IsWindow(hwnd_))
            DestroyWindow(hwnd_);
        if (playingIcon_)
            DestroyIcon(playingIcon_);
        if (font_)
            DeleteObject(font_);
    }
    int run(HINSTANCE instance, int show) {
        openWindow(instance);
        ShowWindow(hwnd_, restoredShow_ ? restoredShow_ : show);
        UpdateWindow(hwnd_);
        if (!store_.warning().empty())
            MessageBoxW(hwnd_, wide(store_.warning()).c_str(), L"QuickStation recovery",
                        MB_OK | MB_ICONINFORMATION);
        return messageLoop();
    }

  private:
#ifdef QUICKSTATION_UI_TESTS
    friend int runNativeUiChecks(HINSTANCE, const fs::path&);
#endif
    void openWindow(HINSTANCE instance) {
        instance_ = instance;
        WNDCLASSEXW wc{sizeof(wc)};
        wc.hInstance = instance;
        wc.lpfnWndProc = windowProc;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
        wc.hIconSm = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                                   GetSystemMetrics(SM_CXSMICON),
                                                   GetSystemMetrics(SM_CYSMICON), LR_SHARED));
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"QuickStation.Native";
        if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            throw std::runtime_error("Window registration failed.");
        UINT dpi = GetDpiForSystem();
        RECT rect{0, 0, MulDiv(660, dpi, 96), MulDiv(510, dpi, 96)};
        AdjustWindowRectExForDpi(&rect, WS_OVERLAPPEDWINDOW, TRUE, 0, dpi);
        // Group boxes are sibling controls and do not fill their interiors. Let
        // the native window background erase old control positions before repaint.
        hwnd_ = CreateWindowExW(0, wc.lpszClassName, L"QuickStation", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                                CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top, nullptr,
                                nullptr, instance, this);
        if (!hwnd_)
            throw std::runtime_error("Could not create the main window.");
        if (auto placement = loadWindowPlacement(store_.directory() / L"window.json")) {
            const auto show = placement->showCmd;
            placement->showCmd = SW_HIDE;
            if (SetWindowPlacement(hwnd_, &*placement))
                restoredShow_ = static_cast<int>(show);
        }
    }
    int messageLoop() {
        ACCEL keys[] = {{FVIRTKEY | FCONTROL, 'F', Find},
                        {FVIRTKEY | FCONTROL, 'N', Add},
                        {FVIRTKEY | FCONTROL, 'M', Mute},
                        {FVIRTKEY | FCONTROL, 'O', Import},
                        {FVIRTKEY | FCONTROL, 'E', Export},
                        {FVIRTKEY | FCONTROL, VK_SPACE, Play},
                        {FVIRTKEY, VK_F1, Usage}};
        HACCEL accel = CreateAcceleratorTableW(keys, static_cast<int>(std::size(keys)));
        if (!accel)
            throw std::runtime_error("Could not create keyboard shortcuts.");
        MSG msg{};
        BOOL status;
        while ((status = GetMessageW(&msg, nullptr, 0, 0)) > 0) {
            if (!TranslateAcceleratorW(hwnd_, accel, &msg)) {
                if (!IsDialogMessageW(hwnd_, &msg)) {
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
            }
        }
        DestroyAcceleratorTable(accel);
        if (status == -1)
            throw std::runtime_error("Windows message loop failed.");
        return static_cast<int>(msg.wParam);
    }

    Store store_;
    Library library_;
    AudioPlayer audio_;
    Network network_;
    Logos logos_, playingLogos_;
    ButtonIcon playButtonIcon_, muteButtonIcon_;
    HINSTANCE instance_{};
    HWND hwnd_{}, page_{}, list_{}, filter_{};
    HFONT font_{};
    HICON playingIcon_{};
    UINT dpi_ = 96;
    int restoredShow_ = 0;
    bool favorites_ = true, dirty_ = false, saveFailed_ = false, rebuilding_ = false;
    Clock::time_point saveAfter_{};
    std::vector<size_t> visible_;
    Playback playback_;
    std::string selectedId_, playingLogo_;
    std::vector<std::pair<std::string, bool>> pendingFavorites_;
    int px(int value) const {
        return MulDiv(value, dpi_, 96);
    }
    HWND control(int id) const {
        if (id == Stations || id == Filter || id == Hint)
            return GetDlgItem(page_, id);
        return GetDlgItem(hwnd_, id);
    }
    HWND child(int id, const wchar_t* type, const wchar_t* text, DWORD style, DWORD exStyle = 0) {
        const auto parent = (id == Stations || id == Filter || id == Hint) ? page_ : hwnd_;
        auto window =
            CreateWindowExW(exStyle, type, text, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | style, 0, 0, 0, 0,
                            parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_, nullptr);
        if (!window)
            throw std::runtime_error("Cannot create a Windows control.");
        return window;
    }
    void movePixels(int id, int x, int y, int width, int height) {
        if (id == Filter)
            placeCenteredEdit(control(id), x, y, width, height);
        else
            placeControl(control(id), x, y, width, height);
    }
    void move(int id, int x, int y, int width, int height) {
        movePixels(id, px(x), px(y), px(width), px(height));
    }
    Station* findStation(std::string_view id) {
        auto found = std::find_if(library_.stations.begin(), library_.stations.end(),
                                  [&](const Station& station) { return station.id == id; });
        return found == library_.stations.end() ? nullptr : &*found;
    }
    Station* rowStation(int row) {
        return row >= 0 && static_cast<size_t>(row) < visible_.size() &&
                       visible_[row] < library_.stations.size()
                   ? &library_.stations[visible_[row]]
                   : nullptr;
    }
    Station* selected() {
        return rowStation(ListView_GetNextItem(list_, -1, LVNI_SELECTED));
    }
    void updateButtonIcons() {
        const bool active =
            playback_.state != PlaybackState::Stopped && playback_.state != PlaybackState::Error;
        playButtonIcon_.apply(control(Play), active ? IDI_STOP : IDI_PLAY, dpi_);
        muteButtonIcon_.apply(control(Mute), library_.muted ? IDI_MUTED : IDI_SPEAKER, dpi_);
    }
    int playbackButtonWidth(int id) const {
        if (!control(id))
            return px(UiSpacing::ButtonWidth);
        return std::max(nativeTextWidth(control(id), id == Play ? L"Play" : L"Mute"),
                        nativeTextWidth(control(id), id == Play ? L"Stop" : L"Unmute")) +
               px(16 + UiSpacing::ButtonIconGap + 2 * UiSpacing::Margin);
    }
    void fonts() {
        NONCLIENTMETRICSW metrics{sizeof(metrics)};
        if (!SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0, dpi_))
            throw std::runtime_error("Cannot read the Windows interface font.");
        auto replacement = CreateFontIndirectW(&metrics.lfMessageFont);
        if (!replacement)
            throw std::runtime_error("Cannot create the Windows interface font.");
        for (int id : {Tabs, Stations, Play, Mute, Volume, Level, Filter, Hint, PlayingGroup, PlayingIcon,
                       PlayingName, PlayingTitle, Status})
            SendMessageW(control(id), WM_SETFONT, reinterpret_cast<WPARAM>(replacement), TRUE);
        if (font_)
            DeleteObject(font_);
        font_ = replacement;
        // Fixed native tabs let Windows center the label in an exact-height target.
        // Size both tabs for the longer current caption, with equal side padding.
        sizeTabs();
        updateButtonIcons();
    }
    void sizeTabs() {
        auto* tabs = control(Tabs);
        HDC dc = GetDC(tabs);
        if (!dc)
            return;
        auto old = SelectObject(dc, font_);
        SIZE favorites{}, all{};
        const auto count = std::count_if(library_.stations.begin(), library_.stations.end(),
                                         [](const Station& station) { return station.favorite; });
        const auto caption = L"Favorites (" + std::to_wstring(count) + L")";
        GetTextExtentPoint32W(dc, caption.c_str(), static_cast<int>(caption.size()), &favorites);
        GetTextExtentPoint32W(dc, L"All stations", 12, &all);
        SelectObject(dc, old);
        ReleaseDC(tabs, dc);
        TabCtrl_SetPadding(tabs, px(UiSpacing::Section), 3);
        TabCtrl_SetItemSize(tabs, std::max(favorites.cx, all.cx) + 2 * px(UiSpacing::Section), px(34));
    }
    void resetLogos() {
        // Detach the old image list before destroying it. The control never owns this list.
        ListView_SetImageList(list_, nullptr, LVSIL_SMALL);
        logos_.reset(px(UiSpacing::Logo), LoadIconW(instance_, MAKEINTRESOURCEW(1)),
                     UiSpacing::ListImageInset, px(UiSpacing::RowPadding), px(UiSpacing::Gap));
        playingLogos_.reset(px(UiSpacing::PlayingLogo), LoadIconW(instance_, MAKEINTRESOURCEW(1)));
        ListView_SetImageList(list_, logos_.images(), LVSIL_SMALL);
        playingLogo_.clear();
    }
    void create() {
        dpi_ = GetDpiForWindow(hwnd_);
        child(Tabs, WC_TABCONTROLW, L"Station views", WS_TABSTOP | WS_CLIPCHILDREN | TCS_SINGLELINE | TCS_FIXEDWIDTH,
              WS_EX_CONTROLPARENT);
        TCITEMW tab{};
        tab.mask = TCIF_TEXT;
        tab.pszText = const_cast<wchar_t*>(L"Favorites");
        TabCtrl_InsertItem(control(Tabs), 0, &tab);
        tab.pszText = const_cast<wchar_t*>(L"All stations");
        TabCtrl_InsertItem(control(Tabs), 1, &tab);
        page_ = CreateDialogParamW(instance_, MAKEINTRESOURCEW(IDD_STATION_PAGE), control(Tabs), pageProc,
                                   reinterpret_cast<LPARAM>(this));
        if (!page_)
            throw std::runtime_error("Cannot create the station tab page.");
        child(Filter, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE);
        filter_ = control(Filter);
        attachCenteredEdit(filter_);
        SendMessageW(filter_, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Filter saved stations..."));
        SendMessageW(filter_, EM_SETLIMITTEXT, 256, 0);
        child(Hint, L"STATIC", L"", SS_LEFT | SS_NOPREFIX);
        createStationList();
        child(PlayingGroup, L"BUTTON", L"Now playing", BS_GROUPBOX);
        child(PlayingIcon, L"STATIC", L"", SS_ICON | SS_CENTERIMAGE);
        child(PlayingName, L"STATIC", L"Choose a favorite to listen", SS_LEFT | SS_NOPREFIX | SS_ENDELLIPSIS);
        child(PlayingTitle, L"STATIC", L"Ready", SS_LEFT | SS_NOPREFIX | SS_ENDELLIPSIS);
        child(Play, L"BUTTON", L"Play", WS_TABSTOP | BS_PUSHBUTTON | BS_LEFT);
        child(Mute, L"BUTTON", library_.muted ? L"Unmute" : L"Mute", WS_TABSTOP | BS_PUSHBUTTON | BS_LEFT);
        const auto volume =
            child(Volume, TRACKBAR_CLASSW, L"Volume", WS_TABSTOP | TBS_NOTICKS | TBS_BOTH | TBS_TOOLTIPS);
        if (!SetWindowSubclass(volume, volumeClickProc, VolumeClickSubclass, 0))
            throw std::runtime_error("Cannot configure volume slider clicks.");
        child(Level, L"STATIC", L"Audio level", SS_OWNERDRAW);
        child(Status, STATUSCLASSNAMEW, L"", SBARS_SIZEGRIP | SBARS_TOOLTIPS);
        SetWindowPos(control(PlayingGroup), HWND_BOTTOM, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        SendMessageW(control(Volume), TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
        SendMessageW(control(Volume), TBM_SETPOS, TRUE, library_.volume);
        createMenuBar();
        fonts();
        resetLogos();
        rebuild();
        updateDisplay();
        if (!SetTimer(hwnd_, UiTimer, 100, nullptr))
            throw std::runtime_error("Cannot start interface updates.");
    }
    void createStationList() {
        const bool hadFocus = list_ && GetFocus() == list_;
        rebuilding_ = true;
        if (list_) {
            ListView_SetImageList(list_, nullptr, LVSIL_SMALL);
            DestroyWindow(list_);
        }
        list_ = child(Stations, WC_LISTVIEWW, L"Stations",
                      WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS, WS_EX_CLIENTEDGE);
        ListView_SetExtendedListViewStyle(list_, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER |
                                                     (favorites_ ? 0 : LVS_EX_CHECKBOXES));
        SetWindowTheme(list_, L"Explorer", nullptr);
        for (auto [column, text] :
             {std::pair{0, L"Station"}, std::pair{1, L"Genre"}, std::pair{2, L"Playback"}}) {
            LVCOLUMNW item{};
            item.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
            item.pszText = const_cast<wchar_t*>(text);
            item.cx = px(120);
            item.iSubItem = column;
            if (ListView_InsertColumn(list_, column, &item) < 0)
                throw std::runtime_error("Cannot create station columns.");
        }
        keepListFocusVisible(list_);
        SendMessageW(list_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), FALSE);
        if (logos_.images())
            ListView_SetImageList(list_, logos_.images(), LVSIL_SMALL);
        rebuilding_ = false;
        if (hadFocus)
            SetFocus(list_);
    }
    void layout() {
        if (!page_ || IsIconic(hwnd_))
            return;
        RECT r{};
        GetClientRect(hwnd_, &r);
        if (r.right <= 0 || r.bottom <= 0)
            return;
        const int width = MulDiv(r.right, 96, dpi_), height = MulDiv(r.bottom, 96, dpi_);
        SendMessageW(control(Status), WM_SIZE, 0, 0);
        RECT statusRect{};
        GetWindowRect(control(Status), &statusRect);
        const int statusHeight = std::max(22, MulDiv(statusRect.bottom - statusRect.top, 96, dpi_));
        const int savedWidth = nativeTextWidth(control(Status), L"Saved locally") + px(2 * UiSpacing::Gap) +
                               GetSystemMetricsForDpi(SM_CXVSCROLL, dpi_);
        const int boundary = std::max(px(100), static_cast<int>(r.right) - savedWidth);
        // Native status bars have no text-margin setter. Empty borderless parts
        // provide equal insets while Windows retains all painting and the grip.
        int parts[] = {px(UiSpacing::Gap), boundary, boundary + px(UiSpacing::Gap), -1};
        SendMessageW(control(Status), SB_SETPARTS, 4, reinterpret_cast<LPARAM>(parts));
        SendMessageW(control(Status), SB_SETTEXTW, 0 | SBT_NOBORDERS, reinterpret_cast<LPARAM>(L""));
        SendMessageW(control(Status), SB_SETTEXTW, 2 | SBT_NOBORDERS, reinterpret_cast<LPARAM>(L""));
        constexpr int artworkTop = 2 * UiSpacing::Section;
        constexpr int controlsTop = artworkTop + UiSpacing::PlayingLogo + UiSpacing::Section;
        constexpr int playbackHeight = controlsTop + UiSpacing::Button + UiSpacing::Section;
        // The native group caption and bottom border leave a two-pixel optical
        // difference on each side. Lift the entire content block by two screen
        // pixels so its visible top and bottom gaps match.
        constexpr int playingVisualOffset = 2;
        const int groupTop = height - statusHeight - playbackHeight - UiSpacing::Margin;
        move(Tabs, UiSpacing::Margin, UiSpacing::Margin, width - 2 * UiSpacing::Margin,
             groupTop - UiSpacing::Margin - UiSpacing::Section);
        RECT pageRect{};
        GetClientRect(control(Tabs), &pageRect);
        TabCtrl_AdjustRect(control(Tabs), FALSE, &pageRect);
        const int pageWidth = std::max(0, static_cast<int>(pageRect.right - pageRect.left));
        const int pageHeight = std::max(0, static_cast<int>(pageRect.bottom - pageRect.top));
        if (page_) {
            SetWindowPos(page_, HWND_TOP, pageRect.left, pageRect.top, pageWidth, pageHeight,
                         SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_NOREDRAW);
        }
        const int contentWidth = MulDiv(pageWidth, 96, dpi_);
        ShowWindow(filter_, favorites_ ? SW_HIDE : SW_SHOW);
        ShowWindow(control(Hint), visible_.empty() ? SW_SHOW : SW_HIDE);
        int top = px(UiSpacing::Gap);
        if (!favorites_) {
            const int fieldHeight = nativeFieldHeight(filter_, dpi_);
            movePixels(Filter, px(UiSpacing::Gap), top, pageWidth - px(2 * UiSpacing::Gap), fieldHeight);
            top += fieldHeight + px(UiSpacing::Gap);
        }
        if (visible_.empty()) {
            const int hintHeight = nativeTextHeight(control(Hint));
            movePixels(Hint, px(UiSpacing::Gap), top, pageWidth - px(2 * UiSpacing::Gap), hintHeight);
            top += hintHeight + px(UiSpacing::Gap);
        }
        movePixels(Stations, px(UiSpacing::Gap), top, pageWidth - px(2 * UiSpacing::Gap),
                   pageHeight - top - px(UiSpacing::Gap));
        ListView_SetColumnWidth(list_, 0, px(std::max(130, (contentWidth - 134) * 3 / 5)));
        ListView_SetColumnWidth(list_, 1, px(std::max(100, (contentWidth - 134) * 2 / 5)));
        ListView_SetColumnWidth(list_, 2, px(92));
        move(PlayingGroup, UiSpacing::Margin, groupTop, width - 2 * UiSpacing::Margin, playbackHeight);
        POINT content{px(UiSpacing::Gap), 0};
        MapWindowPoints(page_, hwnd_, &content, 1);
        const int left = content.x, right = r.right - left;
        const int artworkY = px(groupTop + artworkTop) - playingVisualOffset;
        movePixels(PlayingIcon, left, artworkY, px(UiSpacing::PlayingLogo), px(UiSpacing::PlayingLogo));
        const int textHeight = nativeTextHeight(control(PlayingName)),
                  nameX = left + px(UiSpacing::PlayingLogo + UiSpacing::Margin);
        const int nameY = artworkY +
                          (px(UiSpacing::PlayingLogo) - 2 * textHeight - px(UiSpacing::LabelGap)) / 2;
        movePixels(PlayingName, nameX, nameY, right - nameX, textHeight);
        movePixels(PlayingTitle, nameX, nameY + textHeight + px(UiSpacing::LabelGap), right - nameX,
                   textHeight);
        const int center = px(groupTop + controlsTop) - playingVisualOffset + px(UiSpacing::Button) / 2,
                  button = px(UiSpacing::Button);
        const int playWidth = playbackButtonWidth(Play), muteWidth = playbackButtonWidth(Mute);
        // The themed button paints its left border one physical pixel inside
        // its control. Match that visible edge to the artwork's edge.
        const int controlsLeft = left - 1;
        movePixels(Play, controlsLeft, center - button / 2, playWidth, button);
        const int muteLeft = controlsLeft + playWidth + px(UiSpacing::Gap);
        const int volumeLeft = muteLeft + muteWidth + px(UiSpacing::Gap);
        const int levelSize = button;
        const int levelLeft = right - levelSize;
        const int volumeWidth = levelLeft - px(UiSpacing::Gap) - volumeLeft;
        movePixels(Mute, muteLeft, center - button / 2, muteWidth, button);
        movePixels(Level, levelLeft, center - button / 2, levelSize, button);
        movePixels(Volume, volumeLeft, center - button / 2, volumeWidth, button);
        RECT channel{};
        SendMessageW(control(Volume), TBM_GETCHANNELRECT, 0, reinterpret_cast<LPARAM>(&channel));
        movePixels(Volume, volumeLeft, center - (channel.top + channel.bottom) / 2, volumeWidth, button);
        // Part text set before the first layout can be discarded when Windows creates
        // the status-bar partitions. Restore it whenever the partitions are resized.
        if (control(PlayingName))
            updateDisplay();
        if (page_)
            RedrawWindow(hwnd_, nullptr, nullptr,
                         RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
    }
    void markDirty() {
        dirty_ = true;
        saveAfter_ = Clock::now() + std::chrono::milliseconds(650);
        updateDisplay();
    }
    void save(Library copy) {
        if (auto* station = selected())
            selectedId_ = station->id;
        store_.save(copy);
        library_ = std::move(copy);
        dirty_ = false;
        saveFailed_ = false;
    }
    void rebuild() {
        const bool hasBoxes = (ListView_GetExtendedListViewStyle(list_) & LVS_EX_CHECKBOXES) != 0;
        if (hasBoxes == favorites_)
            createStationList();
        const int top = ListView_GetTopIndex(list_);
        std::vector<size_t> next;
        std::wstring query = favorites_ ? L"" : controlText(filter_);
        std::transform(query.begin(), query.end(), query.begin(), towlower);
        size_t favoriteCount = 0;
        for (size_t i = 0; i < library_.stations.size(); ++i) {
            const auto& station = library_.stations[i];
            favoriteCount += station.favorite;
            if (favorites_ && !station.favorite)
                continue;
            auto name = wide(station.name + " " + station.genre);
            std::transform(name.begin(), name.end(), name.begin(), towlower);
            if (query.empty() || name.find(query) != name.npos)
                next.push_back(i);
        }
        rebuilding_ = true;
        SendMessageW(list_, WM_SETREDRAW, FALSE, 0);
        try {
            ListView_DeleteAllItems(list_);
            visible_ = std::move(next);
            for (size_t row = 0; row < visible_.size(); ++row) {
                const auto& station = library_.stations[visible_[row]];
                auto name = wide(station.name);
                LVITEMW item{};
                item.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_STATE;
                item.iItem = static_cast<int>(row);
                item.pszText = name.data();
                item.iImage = 0;
                item.stateMask = LVIS_STATEIMAGEMASK;
                item.state = favorites_ ? 0 : INDEXTOSTATEIMAGEMASK(station.favorite ? 2 : 1);
                if (ListView_InsertItem(list_, &item) < 0)
                    throw std::runtime_error("Cannot display the station list.");
                // LVS_EX_CHECKBOXES initializes newly inserted items as unchecked.
                // Apply the stored favorite state after insertion, with notifications suppressed.
                if (!favorites_)
                    ListView_SetCheckState(list_, item.iItem, station.favorite);
                auto genre = wide(station.genre);
                ListView_SetItemText(list_, item.iItem, 1, genre.data());
                if (station.id == selectedId_)
                    ListView_SetItemState(list_, item.iItem, LVIS_SELECTED | LVIS_FOCUSED,
                                          LVIS_SELECTED | LVIS_FOCUSED);
            }
        } catch (...) {
            SendMessageW(list_, WM_SETREDRAW, TRUE, 0);
            rebuilding_ = false;
            InvalidateRect(list_, nullptr, TRUE);
            throw;
        }
        SendMessageW(list_, WM_SETREDRAW, TRUE, 0);
        rebuilding_ = false;
        if (top > 0 && !visible_.empty())
            ListView_EnsureVisible(list_, std::min(top, static_cast<int>(visible_.size()) - 1), FALSE);
        auto caption = L"Favorites (" + std::to_wstring(favoriteCount) + L")";
        TCITEMW tab{};
        tab.mask = TCIF_TEXT;
        tab.pszText = caption.data();
        TabCtrl_SetItem(control(Tabs), 0, &tab);
        sizeTabs();
        setText(control(Hint), !visible_.empty() ? L""
                               : favorites_
                                   ? L"No favorites yet. Browse stations or mark favorites in All stations."
                               : !query.empty() ? L"No matching stations. Change or clear the filter."
                                                : L"No saved stations. Use Stations to browse or add one.");
        updateLogos();
        updatePlaybackRows();
        InvalidateRect(list_, nullptr, TRUE);
    }
    void refresh() {
        rebuild();
        layout();
    }
    void updateLogos() {
        const int first = std::max(0, ListView_GetTopIndex(list_));
        const int count = std::min(32, std::max(12, ListView_GetCountPerPage(list_) + 1));
        for (int row = first; row < first + count && static_cast<size_t>(row) < visible_.size(); ++row) {
            const auto& station = library_.stations[visible_[row]];
            network_.requestLogo(station.logo);
            const int image = logos_.get(station.logo, network_.logoPath(station.logo));
            LVITEMW item{};
            item.mask = LVIF_IMAGE;
            item.iItem = row;
            item.iImage = image;
            ListView_SetItem(list_, &item);
        }
    }
    void updatePlaybackRows() {
        for (size_t row = 0; row < visible_.size(); ++row) {
            const auto& station = library_.stations[visible_[row]];
            const wchar_t* state = L"";
            if (station.id == playback_.stationId) {
                switch (playback_.state) {
                case PlaybackState::Playing:
                    state = L"Playing";
                    break;
                case PlaybackState::Connecting:
                    state = L"Connecting";
                    break;
                case PlaybackState::Reconnecting:
                    state = L"Retrying";
                    break;
                case PlaybackState::Buffering:
                    state = L"Buffering";
                    break;
                case PlaybackState::Error:
                    state = L"Error";
                    break;
                default:
                    break;
                }
            }
            ListView_SetItemText(list_, static_cast<int>(row), 2, const_cast<wchar_t*>(state));
        }
    }
    void updateDisplay(bool logosChanged = false) {
        const auto* station =
            findStation(playback_.stationId.empty() ? library_.lastStation : playback_.stationId);
        setText(control(PlayingName), station ? wide(station->name) : L"Choose a favorite to listen");
        setText(control(PlayingTitle), !playback_.title.empty()
                                           ? wide(playback_.title)
                                           : L"Current song will appear when supplied by the station.");
        const bool active =
            playback_.state != PlaybackState::Stopped && playback_.state != PlaybackState::Error;
        setText(control(Play), active ? L"Stop" : L"Play");
        setText(control(Mute), library_.muted ? L"Unmute" : L"Mute");
        updateButtonIcons();
        const std::string logo = station ? station->logo : "";
        if (station)
            network_.requestLogo(logo); // The playing station may be outside the current list/filter.
        if (logosChanged || logo != playingLogo_ || !playingIcon_) {
            playingLogo_ = logo;
            const auto index = station ? playingLogos_.get(logo, network_.logoPath(logo)) : 0;
            auto replacement = ImageList_GetIcon(playingLogos_.images(), index, ILD_NORMAL);
            if (replacement) {
                SendMessageW(control(PlayingIcon), STM_SETICON, reinterpret_cast<WPARAM>(replacement), 0);
                if (playingIcon_)
                    DestroyIcon(playingIcon_);
                playingIcon_ = replacement;
            }
        }
        auto status = wide(playback_.detail);
        wchar_t text[1024]{};
        SendMessageW(control(Status), SB_GETTEXTW, ConnectionPart, reinterpret_cast<LPARAM>(text));
        if (status != text)
            SendMessageW(control(Status), SB_SETTEXTW, ConnectionPart,
                         reinterpret_cast<LPARAM>(status.c_str()));
        SendMessageW(control(Status), SB_SETTIPTEXTW, ConnectionPart,
                     reinterpret_cast<LPARAM>(status.c_str()));
        const wchar_t* saved = saveFailed_ ? L"Save failed" : dirty_ ? L"Saving..." : L"Saved locally";
        SendMessageW(control(Status), SB_GETTEXTW, SavePart, reinterpret_cast<LPARAM>(text));
        if (std::wstring_view(saved) != text)
            SendMessageW(control(Status), SB_SETTEXTW, SavePart,
                         reinterpret_cast<LPARAM>(saved));
    }
    void playStation(const Station& station, bool force = false) {
        auto snapshot = audio_.snapshot();
        if (!force && snapshot.stationId == station.id && snapshot.state != PlaybackState::Stopped &&
            snapshot.state != PlaybackState::Error)
            return;
        Station copy = station; // The saved library may be replaced while the UI handles another action.
        selectedId_ = copy.id;
        library_.lastStation = copy.id;

        audio_.play(copy);
        markDirty();
        tick();
    }
    void setFavorite(const std::string& id, bool value) {
        auto copy = library_;
        for (auto& station : copy.stations)
            if (station.id == id)
                station.favorite = value;
        save(std::move(copy));
        refresh();
    }
    void toggleFavorite() {
        if (auto* station = selected()) {
            auto id = station->id;
            bool value = !station->favorite;
            setFavorite(id, value);
        }
    }
    void editStation(bool adding) {
        Editor editor;
        if (!adding) {
            auto* s = selected();
            if (!s)
                return;
            editor.station = *s;
        }
        if (DialogBoxParamW(instance_, MAKEINTRESOURCEW(IDD_STATION), hwnd_, editProc,
                            reinterpret_cast<LPARAM>(&editor)) == 1) {
            auto copy = library_;
            for (const auto& s : copy.stations)
                if (s.url == editor.station.url && s.id != editor.station.id)
                    throw std::runtime_error("This stream URL is already in your station list.");
            bool restart = false;
            if (adding) {
                if (copy.stations.size() >= MaxStations)
                    throw std::runtime_error("Station limit is 5,000.");
                copy.stations.push_back(editor.station);
            } else
                for (auto& s : copy.stations)
                    if (s.id == editor.station.id) {
                        restart = s.url != editor.station.url && playback_.stationId == s.id;
                        s = editor.station;
                    }
            save(std::move(copy));
            selectedId_ = editor.station.id;
            refresh();
            if (restart)
                playStation(editor.station, true);
        }
    }
    void createMenuBar() {
        NativeMenu bar(false), stations, options, help;
        stations.append(MF_STRING, Find, L"&Browse stations...\tCtrl+F");
        stations.append(MF_STRING, Add, L"&Add by URL...\tCtrl+N");
        stations.append(MF_SEPARATOR, 0, nullptr);
        stations.append(MF_STRING, Import, L"&Import...\tCtrl+O");
        stations.append(MF_STRING, Export, L"&Export...\tCtrl+E");
        options.append(MF_STRING, DoubleClick, L"&Double-click to play");
        options.append(MF_STRING, Mute, L"&Mute\tCtrl+M");
        help.append(MF_STRING, Usage, L"&Using QuickStation\tF1");
        help.append(MF_STRING, About, L"&About QuickStation");
        bar.submenu(stations, L"&Stations");
        bar.submenu(options, L"&Options");
        bar.submenu(help, L"&Help");
        if (!SetMenu(hwnd_, bar.get()))
            throw std::runtime_error("Cannot attach the menu bar.");
        bar.release();
        DrawMenuBar(hwnd_);
    }
    int contextTarget(LPARAM position, POINT& point) {
        point = {GET_X_LPARAM(position), GET_Y_LPARAM(position)};
        int row = -1;
        if (position == -1) {
            row = ListView_GetNextItem(list_, -1, LVNI_SELECTED);
            if (row < 0)
                row = ListView_GetNextItem(list_, -1, LVNI_FOCUSED);
            RECT item{};
            if (row < 0 || !ListView_GetItemRect(list_, row, &item, LVIR_BOUNDS))
                return -1;
            point = {item.left + px(UiSpacing::Section), item.bottom};
            ClientToScreen(list_, &point);
        } else {
            LVHITTESTINFO hit{};
            hit.pt = point;
            ScreenToClient(list_, &hit.pt);
            row = ListView_HitTest(list_, &hit);
            if (row < 0 || !(hit.flags & LVHT_ONITEM))
                return -1;
        }
        if (auto* station = rowStation(row)) {
            selectedId_ = station->id;
            ListView_SetItemState(list_, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
            ListView_SetItemState(list_, row, LVIS_SELECTED | LVIS_FOCUSED,
                                  LVIS_SELECTED | LVIS_FOCUSED);
        } else
            return -1;
        return row;
    }
    void menu(LPARAM position) {
        POINT point{};
        const int row = contextTarget(position, point);
        if (row < 0)
            return;
        NativeMenu popup;
        popup.append(MF_STRING, ToggleFavorite, favorites_ ? L"Remove from favorites" : L"Toggle favorite");
        popup.append(MF_STRING, Edit, L"Edit station...");
        popup.append(MF_STRING, MoveUp, L"Move up");
        popup.append(MF_STRING, MoveDown, L"Move down");
        popup.append(MF_STRING, Remove, L"Remove station...");
        const auto visibleRow = static_cast<size_t>(row);
        if (visibleRow == 0)
            EnableMenuItem(popup.get(), MoveUp, MF_BYCOMMAND | MF_GRAYED);
        if (visibleRow + 1 >= visible_.size())
            EnableMenuItem(popup.get(), MoveDown, MF_BYCOMMAND | MF_GRAYED);
        const auto command =
            TrackPopupMenu(popup.get(), TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, hwnd_, nullptr);
        if (command)
            action(command);
    }
    void action(int id) {
        switch (id) {
        case Find:
            browseStations(instance_, hwnd_, network_, [this](const Station& chosen) {
                auto copy = library_;
                bool exists = false;
                for (auto& station : copy.stations)
                    if (station.url == chosen.url) {
                        station.favorite = true;
                        if (station.logo.empty())
                            station.logo = chosen.logo;
                        exists = true;
                    }
                if (!exists)
                    mergeStations(copy, {chosen});
                save(std::move(copy));
                refresh();
            });
            break;
        case Add:
            editStation(true);
            break;
        case Edit:
            editStation(false);
            break;
        case ToggleFavorite:
            toggleFavorite();
            break;
        case MoveUp:
        case MoveDown: {
            auto* s = selected();
            if (!s)
                break;
            auto copy = library_;
            const auto i = static_cast<size_t>(s - library_.stations.data());
            const auto found = std::find(visible_.begin(), visible_.end(), i);
            if (found != visible_.end() &&
                ((id == MoveUp && found != visible_.begin()) ||
                 (id == MoveDown && found + 1 != visible_.end()))) {
                const auto neighbor = id == MoveUp ? *(found - 1) : *(found + 1);
                selectedId_ = s->id;
                std::swap(copy.stations[i], copy.stations[neighbor]);
                save(std::move(copy));
                refresh();
            }
            break;
        }
        case Remove: {
            auto* s = selected();
            if (!s)
                break;
            auto sid = s->id;
            auto prompt = L"Remove “" + wide(s->name) + L"” from your saved stations?";
            if (MessageBoxW(hwnd_, prompt.c_str(), L"Remove station",
                            MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES) {
                auto copy = library_;
                std::erase_if(copy.stations, [&](const auto& station) { return station.id == sid; });
                if (copy.lastStation == sid)
                    copy.lastStation.clear();
                save(std::move(copy));
                if (playback_.stationId == sid)
                    audio_.stop();
                refresh();
            }
            break;
        }
        case Play:
            if (playback_.state != PlaybackState::Stopped && playback_.state != PlaybackState::Error) {
                audio_.stop();

            } else {
                auto* station = findStation(library_.lastStation);
                if (!station)
                    station = selected();
                if (!station && !visible_.empty())
                    station = &library_.stations[visible_.front()];
                if (station)
                    playStation(*station);
            }
            tick();
            break;
        case Mute:
            library_.muted = !library_.muted;
            markDirty();
            audio_.setVolume(library_.volume, library_.muted);
            SetWindowTextW(control(Mute), library_.muted ? L"Unmute" : L"Mute");
            updateDisplay();
            break;
        case DoubleClick: {
            auto copy = library_;
            copy.doubleClick = !copy.doubleClick;
            save(std::move(copy));
            refresh();
            break;
        }
        case Import:
            if (auto path = chooseFile(hwnd_, false)) {
                auto stations = parsePlaylist(readFile(*path));
                auto copy = library_;
                auto count = mergeStations(copy, stations);
                save(std::move(copy));
                refresh();
                MessageBoxW(
                    hwnd_,
                    (std::to_wstring(count) + L" stations imported. Existing stations were kept.").c_str(),
                    L"Import complete", MB_OK);
            }
            break;
        case Export:
            if (auto path = chooseFile(hwnd_, true)) {
                auto extension = path->extension().wstring();
                std::transform(extension.begin(), extension.end(), extension.begin(), towlower);
                atomicWrite(*path,
                            (extension == L".m3u" || extension == L".m3u8") ? writePlaylist(library_.stations)
                                                                            : encode(library_),
                            false);
            }
            break;
        case Usage: {
            const std::wstring text =
                std::wstring(library_.doubleClick ? L"Double-click a station to listen."
                                                  : L"Click a station to listen.") +
                L" You can also select a station and press Enter.\n\n"
                L"In All stations, checkboxes mark favorites. Right-click a station to edit, "
                L"reorder, remove it or change its favorite status.\n\n"
                L"Options → Double-click to play changes the activation gesture.\n"
                L"Ctrl+Space: play/stop · Ctrl+M: mute\n"
                L"Ctrl+F: browse · Ctrl+N: add a stream URL\n"
                L"Ctrl+O: import · Ctrl+E: export\n\n"
                L"Stop disconnects the stream. Play returns to its current live broadcast.\n"
                L"Saved stations play independently of the online station directory.";
            MessageBoxW(hwnd_, text.c_str(), L"Using QuickStation", MB_OK | MB_ICONINFORMATION);
            break;
        }
        case About:
            MessageBoxW(
                hwnd_,
                L"QuickStation 1.6.14\nA small, native C++ radio player.\n\n"
                L"Help → Using QuickStation explains the controls and keyboard shortcuts.\n\n"
                L"Stations and settings: local JSON with recovery "
                L"backup.\nDirectory: Radio Browser · Audio: BASS / BASSHLS\nBASS is free for non-commercial "
                L"use; commercial use requires a license.\n\nNo account or cloud synchronization.",
                L"About QuickStation", MB_OK | MB_ICONINFORMATION);
            break;
        }
    }
    void tick() {
        auto snapshot = audio_.snapshot();
        if (snapshot != playback_) {
            const bool statusChanged = snapshot.state != playback_.state ||
                                       snapshot.stationId != playback_.stationId ||
                                       snapshot.title != playback_.title ||
                                       snapshot.detail != playback_.detail ||
                                       snapshot.attempts != playback_.attempts;
            const bool levelChanged = snapshot.level != playback_.level;
            playback_ = std::move(snapshot);
            if (statusChanged) {
                updatePlaybackRows();
                updateDisplay();
            }
            if (statusChanged || levelChanged)
                InvalidateRect(control(Level), nullptr, FALSE);
        }
        if (network_.takeLogoChange()) {
            updateLogos();
            updateDisplay(true);
        }
        if (dirty_ && Clock::now() >= saveAfter_) {
            try {
                store_.save(library_);
                dirty_ = false;
                saveFailed_ = false;
                updateDisplay();
            } catch (const std::exception& error) {
                dirty_ = false;
                saveFailed_ = true;
                updateDisplay();
                errorBox(hwnd_, error);
            }
        }
    }
    LRESULT notify(const NMHDR& header, LPARAM lp) {
        if (header.idFrom == Tabs && header.code == TCN_SELCHANGE) {
            KillTimer(hwnd_, FilterTimer);
            favorites_ = TabCtrl_GetCurSel(control(Tabs)) == 0;
            rebuild();
            layout();
            return 0;
        }
        if (header.idFrom != Stations || rebuilding_)
            return 0;
        if (header.code == LVN_ITEMCHANGED) {
            const auto& changed = *reinterpret_cast<NMLISTVIEW*>(lp);
            if (auto* station = rowStation(changed.iItem)) {
                if ((changed.uChanged & LVIF_STATE) && (changed.uNewState & LVIS_SELECTED))
                    selectedId_ = station->id;
                if ((changed.uChanged & LVIF_STATE) &&
                    ((changed.uOldState ^ changed.uNewState) & LVIS_STATEIMAGEMASK)) {
                    const bool checked =
                        (changed.uNewState & LVIS_STATEIMAGEMASK) == INDEXTOSTATEIMAGEMASK(2);
                    if (checked != station->favorite) {
                        const bool post = pendingFavorites_.empty();
                        pendingFavorites_.emplace_back(station->id, checked);
                        if (post && !PostMessageW(hwnd_, ApplyFavorites, 0, 0)) {
                            pendingFavorites_.clear();
                            throw std::runtime_error("Cannot update favorites.");
                        }
                    }
                }
            }
        } else if (header.code == NM_CLICK || header.code == NM_DBLCLK) {
            const auto& clicked = *reinterpret_cast<NMITEMACTIVATE*>(lp);
            LVHITTESTINFO hit{};
            hit.pt = clicked.ptAction;
            ListView_HitTest(list_, &hit);
            if (hit.flags & LVHT_ONITEMSTATEICON)
                return 0;
            if ((header.code == NM_CLICK && !library_.doubleClick) ||
                (header.code == NM_DBLCLK && library_.doubleClick))
                if (auto* station = rowStation(clicked.iItem))
                    playStation(*station);
        } else if (header.code == LVN_KEYDOWN) {
            const auto& key = *reinterpret_cast<NMLVKEYDOWN*>(lp);
            if (key.wVKey == VK_RETURN)
                if (auto* station = selected())
                    playStation(*station);
        } else if (header.code == LVN_ENDSCROLL) {
            updateLogos();
        }
        return 0;
    }
    void applyFavorites() {
        auto pending = std::move(pendingFavorites_);
        pendingFavorites_.clear();
        if (pending.empty())
            return;
        auto copy = library_;
        for (const auto& [id, value] : pending)
            for (auto& station : copy.stations)
                if (station.id == id)
                    station.favorite = value;
        try {
            save(std::move(copy));
            refresh();
        } catch (...) {
            rebuild();
            throw;
        } // Restore native checkboxes to the last committed library.
    }
    static INT_PTR CALLBACK pageProc(HWND page, UINT msg, WPARAM wp, LPARAM lp) {
        auto* app = reinterpret_cast<App*>(GetWindowLongPtrW(page, DWLP_USER));
        if (msg == WM_INITDIALOG) {
            SetWindowLongPtrW(page, DWLP_USER, lp);
            EnableThemeDialogTexture(page, ETDT_ENABLETAB);
            return TRUE;
        }
        if (msg == WM_CTLCOLORSTATIC || msg == WM_CTLCOLOREDIT)
            if (auto brush = nativeFieldBrush(reinterpret_cast<HWND>(lp), reinterpret_cast<HDC>(wp)))
                return reinterpret_cast<INT_PTR>(brush);
        // The native child dialog owns the station controls; their events still use
        // the main window's existing controller and exception boundary.
        if (app && (msg == WM_COMMAND || msg == WM_NOTIFY || msg == WM_CONTEXTMENU)) {
            const auto result = SendMessageW(app->hwnd_, msg, wp, lp);
            SetWindowLongPtrW(page, DWLP_MSGRESULT, result);
            return TRUE;
        }
        return FALSE;
    }
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        auto* app = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (msg == WM_NCCREATE) {
            app = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            app->hwnd_ = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        }
        if (!app)
            return DefWindowProcW(hwnd, msg, wp, lp);
        try {
            switch (msg) {
            case WM_CREATE:
                app->create();
                return 0;
            case WM_SIZE:
                if (wp == SIZE_MINIMIZED)
                    return 0;
                app->layout();
                return 0;
            case WM_GETMINMAXINFO: {
                auto* info = reinterpret_cast<MINMAXINFO*>(lp);
                info->ptMinTrackSize = {
                    std::max(app->px(520),
                             app->playbackButtonWidth(Play) + app->playbackButtonWidth(Mute) + app->px(224)),
                    app->px(430)};
                return 0;
            }
            case WM_DPICHANGED: {
                app->dpi_ = HIWORD(wp);
                app->fonts();
                app->resetLogos();
                app->rebuild();
                auto* r = reinterpret_cast<RECT*>(lp);
                SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                             SWP_NOZORDER | SWP_NOACTIVATE);
                app->layout();
                app->updateDisplay(true);
                return 0;
            }
            case WM_SETTINGCHANGE:
                if (wp == SPI_SETNONCLIENTMETRICS) {
                    app->fonts();
                    app->layout();
                }
                break;
            case WM_THEMECHANGED:
            case WM_SYSCOLORCHANGE:
                app->updateButtonIcons();
                RedrawWindow(hwnd, nullptr, nullptr,
                             RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
                break;
            case WM_HSCROLL:
                if (reinterpret_cast<HWND>(lp) == app->control(Volume)) {
                    app->library_.volume =
                        static_cast<int>(SendMessageW(app->control(Volume), TBM_GETPOS, 0, 0));
                    app->audio_.setVolume(app->library_.volume, app->library_.muted);
                    app->markDirty();
                }
                return 0;
            case WM_DRAWITEM:
                if (wp == Level) {
                    drawAudioLevel(*reinterpret_cast<DRAWITEMSTRUCT*>(lp), app->playback_.level,
                                   app->dpi_);
                    return TRUE;
                }
                break;
            case WM_INITMENUPOPUP:
                CheckMenuItem(GetMenu(hwnd), DoubleClick,
                              MF_BYCOMMAND | (app->library_.doubleClick ? MF_CHECKED : MF_UNCHECKED));
                CheckMenuItem(GetMenu(hwnd), Mute,
                              MF_BYCOMMAND | (app->library_.muted ? MF_CHECKED : MF_UNCHECKED));
                break;
            case WM_NOTIFY:
                return app->notify(*reinterpret_cast<NMHDR*>(lp), lp);
            case ApplyFavorites:
                app->applyFavorites();
                return 0;
            case WM_COMMAND:
                if (LOWORD(wp) == Filter) {
                    if (HIWORD(wp) == EN_CHANGE && !app->favorites_ &&
                        !SetTimer(hwnd, FilterTimer, 120, nullptr))
                        app->refresh();
                } else
                    app->action(LOWORD(wp));
                return 0;
            case WM_CONTEXTMENU:
                if (reinterpret_cast<HWND>(wp) == app->list_)
                    app->menu(lp);
                return 0;
            case WM_TIMER:
                if (wp == UiTimer)
                    app->tick();
                else if (wp == FilterTimer) {
                    KillTimer(hwnd, FilterTimer);
                    if (!app->favorites_)
                        app->refresh();
                }
                return 0;
            case WM_CLOSE: {
                app->applyFavorites();
                if (app->dirty_ || app->saveFailed_) {
                    app->store_.save(app->library_);
                    app->dirty_ = false;
                    app->saveFailed_ = false;
                }
                try {
                    WINDOWPLACEMENT placement{sizeof(placement)};
                    if (!GetWindowPlacement(hwnd, &placement))
                        throw std::runtime_error("Cannot read the window position.");
                    saveWindowPlacement(app->store_.directory() / L"window.json", placement);
                } catch (const std::exception& error) {
                    // Optional layout persistence must not prevent the application from quitting.
                    errorBox(hwnd, error);
                }
                app->audio_.stop();
                DestroyWindow(hwnd);
                return 0;
            }
            case WM_DESTROY:
                if (IsWindow(app->list_))
                    ListView_SetImageList(app->list_, nullptr, LVSIL_SMALL);
                KillTimer(hwnd, UiTimer);
                KillTimer(hwnd, FilterTimer);
                PostQuitMessage(0);
                return 0;
            case WM_NCDESTROY:
                app->hwnd_ = nullptr;
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
                break;
            }
        } catch (const std::exception& error) {
            errorBox(hwnd, error);
            if (msg == WM_CREATE)
                return -1;
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
};
#ifdef QUICKSTATION_UI_TESTS
#include "../tests/native_ui_checks.inc"
#endif
int runApplication(HINSTANCE instance, int show, const fs::path& data) {
    App app(data);
    return app.run(instance, show);
}
} // namespace radio
