// Hidden real dialog, isolated profile, and loopback-only metadata requests.
int browserUiChecks(HINSTANCE instance, HWND owner, const fs::path& data) {
    int checks = 0, added = 0;
    auto check = [&](bool value, const char* message) {
        ++checks;
        if (!value)
            throw std::runtime_error(message);
    };
    auto rect = [](HWND window) {
        RECT r{};
        GetWindowRect(window, &r);
        return r;
    };
    Network network(data, {"http://127.0.0.1:1"});
    Browser browser(network, [&](const Station& station) {
        if (station.name == "Test Jazz")
            ++added;
    });
    struct WindowGuard {
        HWND window;
        ~WindowGuard() {
            if (IsWindow(window))
                DestroyWindow(window);
        }
    };
    WindowGuard guard{CreateDialogParamW(instance, MAKEINTRESOURCEW(IDD_BROWSE), owner, Browser::proc,
                                         reinterpret_cast<LPARAM>(&browser))};
    check(guard.window != nullptr, "Native Browse stations dialog created");
    const auto query = browser.control(IDC_QUERY), frame = nativeEditFrame(query),
               list = browser.control(IDC_RESULTS);
    check(FindWindowExW(list, nullptr, L"Static", nullptr) == nullptr &&
              !(ListView_GetExtendedListViewStyle(list) & LVS_EX_GRIDLINES),
          "Browse retains native headers without an added divider or gridlines");
    check(ListView_GetItemCount(list) == 0 && !IsWindowEnabled(browser.control(IDC_ADD)),
          "Empty browser cannot add a station");
    check(ListView_GetColumnWidth(list, 0) > 0 && ListView_GetColumnWidth(list, 1) > 0 &&
              ListView_GetColumnWidth(list, 2) > 0,
          "Station / Country / Genre columns present");
    check(GetNextDlgTabItem(guard.window, browser.control(IDC_COUNTRY), FALSE) ==
              browser.control(IDC_CATEGORY),
          "Country-to-genre keyboard navigation");
    check(GetNextDlgTabItem(guard.window, browser.control(IDC_CATEGORY), FALSE) == query,
          "Genre-to-name keyboard navigation");
    for (int width : {660, 540, 840}) {
        RECT size{0, 0, browser.px(width), browser.px(500)};
        AdjustWindowRectExForDpi(&size, static_cast<DWORD>(GetWindowLongPtrW(guard.window, GWL_STYLE)), FALSE,
                                 static_cast<DWORD>(GetWindowLongPtrW(guard.window, GWL_EXSTYLE)),
                                 GetDpiForWindow(guard.window));
        SetWindowPos(guard.window, nullptr, 0, 0, size.right - size.left, size.bottom - size.top,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        auto input = rect(frame), table = rect(list), browse = rect(browser.control(IDC_FIND)),
             add = rect(browser.control(IDC_ADD)), close = rect(browser.control(IDCANCEL));
        RECT format{};
        SendMessageW(query, EM_GETRECT, 0, reinterpret_cast<LPARAM>(&format));
        const auto inner = rect(query);
        check(abs(inner.top + format.top + nativeTextHeight(query) / 2 - (input.top + input.bottom) / 2) <= 1,
              "Browse name text centered in taller frame");
        check(input.left == table.left && browse.right == table.right && close.right == table.right,
              "Browser content edges share a margin");
        check(abs((input.top + input.bottom) / 2 - (browse.top + browse.bottom) / 2) <= 1,
              "Browse button aligns with input center");
        check(add.top == close.top && add.bottom == close.bottom && add.right < close.left,
              "Consistent dialog action buttons and spacing");
        const auto country = rect(browser.control(IDC_COUNTRY)), genre = rect(browser.control(IDC_CATEGORY));
        check(country.top == genre.top && country.bottom == genre.bottom && country.right < genre.left,
              "Country and genre row alignment");
        const auto label = rect(browser.control(IDC_COUNTRY_LABEL));
        const auto nameLabel = rect(browser.control(IDC_QUERY_LABEL));
        check(country.top - label.bottom == browser.px(UiSpacing::LabelGap) &&
                  nameLabel.top - country.bottom == browser.px(UiSpacing::Section) &&
                  country.bottom - country.top >= nativeTextHeight(query),
              "Native dropdown rows preserve consistent label/section gaps without clipping");
        check(table.bottom <= rect(browser.control(IDC_BROWSE_STATUS)).top,
              "Results do not overlap status text");
    }
    browser.stations_ = {{{newId(), "Test Jazz", "https://test/jazz", "", "jazz", true}, "Tunisia"}};
    LVITEMW item{};
    item.mask = LVIF_TEXT;
    item.pszText = const_cast<wchar_t*>(L"H");
    ListView_InsertItem(list, &item);
    ListView_SetItemText(list, 0, 1, const_cast<wchar_t*>(L"H"));
    ListView_SetItemText(list, 0, 2, const_cast<wchar_t*>(L"H"));
    NativePixels resultPixels(list);
    RECT row{};
    ListView_GetItemRect(list, 0, &row, LVIR_BOUNDS);
    int inset[3]{};
    for (int column = 0; column < 3; ++column) {
        const int left = column == 0 ? 0
                                     : ListView_GetColumnWidth(list, 0) +
                                           (column == 2 ? ListView_GetColumnWidth(list, 1) : 0);
        const int right = left + ListView_GetColumnWidth(list, column);
        int ink = right;
        for (int y = row.top + 2; y < row.bottom - 2; ++y)
            for (int x = left + 1; x < right - 1; ++x)
                if (nativeDarkPixel(resultPixels.at(x, y)))
                    ink = std::min(ink, x);
        inset[column] = ink - left;
    }
    check(abs(inset[0] - inset[1]) <= 1 && abs(inset[1] - inset[2]) <= 1 && inset[0] < 12,
          "Rendered Browse text columns share the native left inset");
    ListView_SetItemState(list, 0, LVIS_SELECTED, LVIS_SELECTED);
    check(IsWindowEnabled(browser.control(IDC_ADD)), "Selecting a browse result enables Add");
    browser.add();
    check(added == 1, "Add favorite callback receives selected directory station");
    browser.busy_ = true;
    browser.begin();
    check(!browser.busy_ && controlText(browser.control(IDC_FIND)) == L"Browse",
          "Cancel returns browser to a usable state");
    return checks;
}
