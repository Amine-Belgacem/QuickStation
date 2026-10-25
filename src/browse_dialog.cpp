#include "browse_dialog.h"
#include "ui_metrics.h"
#ifdef QUICKSTATION_UI_TESTS
#include "../tests/native_render.h"
#endif
#include "../assets/resource.h"
#include <commctrl.h>
#include <uxtheme.h>
#include <stdexcept>

namespace radio {
namespace {
constexpr UINT Relayout = WM_APP + 10;
class Browser {
  public:
    Browser(Network& network, AddFavorite add) : network_(network), add_(std::move(add)) {}
    ~Browser() {
        if (rows_)
            ImageList_Destroy(rows_);
    }
    void run(HINSTANCE instance, HWND owner) {
        if (DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_BROWSE), owner, proc,
                            reinterpret_cast<LPARAM>(this)) == -1)
            throw std::runtime_error("Cannot open the station browser.");
    }

  private:
#ifdef QUICKSTATION_UI_TESTS
    friend int browserUiChecks(HINSTANCE, HWND, const fs::path&);
#endif
    HWND dialog_{};
    Network& network_;
    AddFavorite add_;
    DirectoryFilters filters_;
    std::vector<DirectoryStation> stations_;
    std::string activeGenre_;
    HIMAGELIST rows_{};
    bool busy_ = false, initialized_ = false;
    int px(int value) const {
        return MulDiv(value, GetDpiForWindow(dialog_), 96);
    }
    HWND control(int id) const {
        return GetDlgItem(dialog_, id);
    }
    void status(const std::wstring& value) {
        SetWindowTextW(control(IDC_BROWSE_STATUS), value.c_str());
    }
    void combo(HWND window, const std::wstring& value) {
        if (SendMessageW(window, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value.c_str())) < 0)
            throw std::runtime_error("Cannot display station filters.");
    }
    void initialize() {
        attachCenteredEdit(control(IDC_QUERY));
        SendMessageW(control(IDC_QUERY), EM_SETLIMITTEXT, 128, 0);
        SendMessageW(control(IDC_QUERY), EM_SETCUEBANNER, TRUE,
                     reinterpret_cast<LPARAM>(L"Search by name..."));
        combo(control(IDC_COUNTRY), L"All countries");
        combo(control(IDC_CATEGORY), L"All genres");
        SendMessageW(control(IDC_COUNTRY), CB_SETCURSEL, 0, 0);
        SendMessageW(control(IDC_CATEGORY), CB_SETCURSEL, 0, 0);
        ListView_SetExtendedListViewStyle(control(IDC_RESULTS), LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        SetWindowTheme(control(IDC_RESULTS), L"Explorer", nullptr);
        for (auto [index, label] :
             {std::pair{0, L"Station"}, std::pair{1, L"Country"}, std::pair{2, L"Genre"}}) {
            LVCOLUMNW column{};
            column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
            column.pszText = const_cast<wchar_t*>(label);
            column.cx = px(150);
            column.iSubItem = index;
            if (ListView_InsertColumn(control(IDC_RESULTS), index, &column) < 0)
                throw std::runtime_error("Cannot create browse columns.");
        }
        keepListFocusVisible(control(IDC_RESULTS));
        initialized_ = true;
        layout();
        centerDialogOnOwner(dialog_);
        EnableWindow(control(IDC_ADD), FALSE);
        network_.cancelSearch();
        network_.requestFilters();
        status(L"Loading countries and genres from Radio Browser...");
        if (!SetTimer(dialog_, 1, 150, nullptr))
            throw std::runtime_error("Cannot update the station browser.");
    }
    void layout() {
        if (!initialized_ || IsIconic(dialog_))
            return;
        RECT r{};
        GetClientRect(dialog_, &r);
        const int margin = px(UiSpacing::Margin), gap = px(UiSpacing::Gap), section = px(UiSpacing::Section);
        const int width = r.right - 2 * margin, line = nativeTextHeight(control(IDC_QUERY));
        const int field = nativeFieldHeight(control(IDC_QUERY), GetDpiForWindow(dialog_));
        const int half = (width - section) / 2, fieldTop = margin + line + px(UiSpacing::LabelGap);
        placeControl(control(IDC_COUNTRY_LABEL), margin, margin, half, line);
        placeControl(control(IDC_CATEGORY_LABEL), margin + half + section, margin, half, line);
        int dropdownHeight = 0;
        for (auto [id, x] :
             {std::pair{IDC_COUNTRY, margin}, std::pair{IDC_CATEGORY, margin + half + section}}) {
            placeControl(control(id), x, fieldTop, half, px(240));
            // Use actual native control heights to preserve the shared label
            // and section gaps without overriding Windows' dropdown rendering.
            RECT comboRect{};
            GetWindowRect(control(id), &comboRect);
            dropdownHeight = std::max(dropdownHeight, static_cast<int>(comboRect.bottom - comboRect.top));
            SendMessageW(control(id), CB_SETDROPPEDWIDTH, half + px(UiSpacing::Section), 0);
        }
        const int nameTop = fieldTop + dropdownHeight + section,
                  queryTop = nameTop + line + px(UiSpacing::LabelGap);
        placeControl(control(IDC_QUERY_LABEL), margin, nameTop, width, line);
        placeCenteredEdit(control(IDC_QUERY), margin, queryTop, width - gap - px(UiSpacing::ButtonWidth),
                          field);
        placeControl(control(IDC_FIND), r.right - margin - px(UiSpacing::ButtonWidth),
                     queryTop + (field - px(UiSpacing::Button)) / 2, px(UiSpacing::ButtonWidth),
                     px(UiSpacing::Button));
        const int buttons = r.bottom - margin - px(UiSpacing::Button), info = buttons - gap - 2 * line;
        const int listTop = queryTop + field + section;
        placeControl(control(IDC_RESULTS), margin, listTop, width, info - gap - listTop);
        const int tableWidth =
            std::max(0, width - 2 * GetSystemMetricsForDpi(SM_CXEDGE, GetDpiForWindow(dialog_)) -
                            GetSystemMetricsForDpi(SM_CXVSCROLL, GetDpiForWindow(dialog_)));
        ListView_SetColumnWidth(control(IDC_RESULTS), 0, tableWidth / 2);
        ListView_SetColumnWidth(control(IDC_RESULTS), 1, tableWidth / 4);
        ListView_SetColumnWidth(control(IDC_RESULTS), 2, tableWidth - tableWidth / 2 - tableWidth / 4);
        int oldWidth = 0, oldHeight = 0;
        if (rows_)
            ImageList_GetIconSize(rows_, &oldWidth, &oldHeight);
        if (oldHeight != field) {
            auto replacement = ImageList_Create(1, field, ILC_COLOR32, 1, 1);
            if (!replacement)
                throw std::runtime_error("Cannot set native browse row spacing.");
            ListView_SetImageList(control(IDC_RESULTS), replacement, LVSIL_SMALL);
            if (rows_)
                ImageList_Destroy(rows_);
            rows_ = replacement;
        }
        placeControl(control(IDC_BROWSE_STATUS), margin, info, width, 2 * line);
        placeControl(control(IDC_SOURCE), margin, buttons + (px(UiSpacing::Button) - line) / 2,
                     width - px(136) - px(UiSpacing::ButtonWidth) - 2 * gap, line);
        placeControl(control(IDC_ADD), r.right - margin - px(UiSpacing::ButtonWidth) - gap - px(136), buttons,
                     px(136), px(UiSpacing::Button));
        placeControl(control(IDCANCEL), r.right - margin - px(UiSpacing::ButtonWidth), buttons,
                     px(UiSpacing::ButtonWidth), px(UiSpacing::Button));
        RedrawWindow(dialog_, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    }
    void begin() {
        if (busy_) {
            network_.cancelSearch();
            busy_ = false;
            SetWindowTextW(control(IDC_FIND), L"Browse");
            status(L"Browsing cancelled. Choose filters and try again.");
            return;
        }
        BrowseQuery query{trim(utf8(controlText(control(IDC_QUERY)))), {}, {}};
        const auto country = SendMessageW(control(IDC_COUNTRY), CB_GETCURSEL, 0, 0);
        const auto genre = SendMessageW(control(IDC_CATEGORY), CB_GETCURSEL, 0, 0);
        if (country > 0 && static_cast<size_t>(country) <= filters_.countries.size())
            query.countryCode = filters_.countries[country - 1].code;
        if (genre > 0 && static_cast<size_t>(genre) <= filters_.genres.size())
            query.genre = filters_.genres[genre - 1];
        browsePath(query); // Validate before changing UI state or submitting a request.
        activeGenre_ = query.genre;
        stations_.clear();
        ListView_DeleteAllItems(control(IDC_RESULTS));
        EnableWindow(control(IDC_ADD), FALSE);
        busy_ = true;
        SetWindowTextW(control(IDC_FIND), L"Cancel");
        status(L"Browsing Radio Browser...");
        network_.search(std::move(query));
        if (filters_.countries.empty())
            network_.requestFilters();
    }
    void poll() {
        if (auto filters = network_.takeFilters()) {
            if (filters->error.empty()) {
                filters_ = std::move(*filters);
                SendMessageW(control(IDC_COUNTRY), CB_RESETCONTENT, 0, 0);
                combo(control(IDC_COUNTRY), L"All countries");
                for (const auto& country : filters_.countries)
                    combo(control(IDC_COUNTRY), wide(country.name));
                SendMessageW(control(IDC_COUNTRY), CB_SETCURSEL, 0, 0);
                SendMessageW(control(IDC_CATEGORY), CB_RESETCONTENT, 0, 0);
                combo(control(IDC_CATEGORY), L"All genres");
                for (const auto& genre : filters_.genres)
                    combo(control(IDC_CATEGORY), wide(genre));
                SendMessageW(control(IDC_CATEGORY), CB_SETCURSEL, 0, 0);
                if (!busy_ && stations_.empty())
                    status(L"Choose a country, genre, or station name, then Browse.");
            } else if (!busy_ && stations_.empty())
                status(L"Directory filters unavailable. Browse to retry; saved stations still work.");
        }
        if (auto result = network_.takeSearch()) {
            busy_ = false;
            SetWindowTextW(control(IDC_FIND), L"Browse");
            stations_ = std::move(result->stations);
            auto list = control(IDC_RESULTS);
            SendMessageW(list, WM_SETREDRAW, FALSE, 0);
            try {
                for (size_t i = 0; i < stations_.size(); ++i) {
                    auto name = wide(stations_[i].station.name);
                    LVITEMW item{};
                    item.mask = LVIF_TEXT;
                    item.iItem = static_cast<int>(i);
                    item.pszText = name.data();
                    if (ListView_InsertItem(list, &item) < 0)
                        throw std::runtime_error("Cannot display stations.");
                    auto country = wide(stations_[i].country),
                         genre = wide(activeGenre_.empty() ? stations_[i].station.genre : activeGenre_);
                    ListView_SetItemText(list, item.iItem, 1, country.data());
                    ListView_SetItemText(list, item.iItem, 2, genre.data());
                }
            } catch (...) {
                SendMessageW(list, WM_SETREDRAW, TRUE, 0);
                throw;
            }
            SendMessageW(list, WM_SETREDRAW, TRUE, 0);
            InvalidateRect(list, nullptr, TRUE);
            if (result->cached)
                status(L"Radio Browser is unavailable. Showing recent results for this search.");
            else if (!result->error.empty())
                status(L"Radio Browser unavailable: " + wide(result->error) +
                       L" Try Browse again later; saved stations still work.");
            else if (stations_.empty())
                status(L"No stations match these filters. Try a different country, genre, or name.");
            else
                status(std::to_wstring(stations_.size()) +
                       L" results (up to 80). Select a station to add to favorites.");
        }
    }
    void add() {
        const auto row = ListView_GetNextItem(control(IDC_RESULTS), -1, LVNI_SELECTED);
        if (row < 0 || static_cast<size_t>(row) >= stations_.size())
            return;
        add_(stations_[row].station);
        status(L"Saved to favorites. You can add another station.");
    }
    static INT_PTR CALLBACK proc(HWND dialog, UINT message, WPARAM wp, LPARAM lp) {
        auto* browser = reinterpret_cast<Browser*>(GetWindowLongPtrW(dialog, DWLP_USER));
        try {
            if (message == WM_INITDIALOG) {
                browser = reinterpret_cast<Browser*>(lp);
                browser->dialog_ = dialog;
                SetWindowLongPtrW(dialog, DWLP_USER, lp);
                browser->initialize();
                return TRUE;
            }
            if (!browser)
                return FALSE;
            if (message == WM_CTLCOLORSTATIC || message == WM_CTLCOLOREDIT) {
                if (auto brush = nativeFieldBrush(reinterpret_cast<HWND>(lp), reinterpret_cast<HDC>(wp)))
                    return reinterpret_cast<INT_PTR>(brush);
            }
            if (message == WM_GETMINMAXINFO) {
                auto& info = *reinterpret_cast<MINMAXINFO*>(lp);
                info.ptMinTrackSize = {browser->px(540), browser->px(420)};
                return TRUE;
            }
            if (message == WM_SIZE || message == Relayout) {
                browser->layout();
                return TRUE;
            }
            if (message == WM_DPICHANGED) {
                PostMessageW(dialog, Relayout, 0, 0);
                return FALSE;
            }
            if (message == WM_TIMER && wp == 1) {
                browser->poll();
                return TRUE;
            }
            if (message == WM_COMMAND) {
                if (LOWORD(wp) == IDCANCEL) {
                    EndDialog(dialog, 0);
                    return TRUE;
                }
                if (LOWORD(wp) == IDC_FIND) {
                    browser->begin();
                    return TRUE;
                }
                if (LOWORD(wp) == IDC_ADD) {
                    browser->add();
                    return TRUE;
                }
            }
            if (message == WM_NOTIFY) {
                const auto& header = *reinterpret_cast<NMHDR*>(lp);
                if (header.idFrom == IDC_RESULTS) {
                    if (header.code == LVN_ITEMCHANGED)
                        EnableWindow(browser->control(IDC_ADD),
                                     ListView_GetNextItem(browser->control(IDC_RESULTS), -1, LVNI_SELECTED) >=
                                         0);
                    if (header.code == NM_DBLCLK)
                        browser->add();
                }
            }
            if (message == WM_DESTROY) {
                browser->network_.cancelSearch();
                KillTimer(dialog, 1);
                ListView_SetImageList(browser->control(IDC_RESULTS), nullptr, LVSIL_SMALL);
                return TRUE;
            }
        } catch (const std::exception& error) {
            MessageBoxW(dialog, wide(error.what()).c_str(), L"QuickStation", MB_OK | MB_ICONERROR);
            if (message == WM_INITDIALOG)
                EndDialog(dialog, 0);
        }
        return FALSE;
    }
};
#ifdef QUICKSTATION_UI_TESTS
#include "../tests/browse_ui_checks.inc"
#endif
} // namespace
void browseStations(HINSTANCE instance, HWND owner, Network& network, AddFavorite add) {
    Browser browser(network, std::move(add));
    browser.run(instance, owner);
}
#ifdef QUICKSTATION_UI_TESTS
int runBrowseUiChecks(HINSTANCE instance, HWND owner, const fs::path& data) {
    return browserUiChecks(instance, owner, data);
}
#endif
} // namespace radio
