// Compiled only into native_ui_tests. These are the production controller and
// native Windows controls, created hidden with an isolated local library.
int runNativeUiChecks(HINSTANCE instance, const fs::path& data) {
    int checks = 0;
    auto check = [&](bool condition, const char* message) {
        ++checks;
        if (!condition)
            throw std::runtime_error(message);
    };
    auto rect = [](HWND window) {
        RECT r{};
        GetWindowRect(window, &r);
        return r;
    };
    auto sameRect = [](RECT a, RECT b) { return EqualRect(&a, &b) != FALSE; };
    App app(data);
    app.openWindow(instance);
    auto selectTab = [&](bool favorites) {
        TabCtrl_SetCurSel(app.control(Tabs), favorites ? 0 : 1);
        NMHDR change{app.control(Tabs), Tabs, TCN_SELCHANGE};
        SendMessageW(app.hwnd_, WM_NOTIFY, Tabs, reinterpret_cast<LPARAM>(&change));
    };
    auto applyFilter = [&](const wchar_t* value) {
        SetWindowTextW(app.filter_, value);
        SendMessageW(app.hwnd_, WM_TIMER, FilterTimer, 0); // Complete the short typing debounce.
    };
    auto editFits = [&](HWND field) {
        const auto outer = rect(nativeEditFrame(field)), inner = rect(field);
        wchar_t className[16]{};
        GetClassNameW(nativeEditFrame(field), className, 16);
        check(std::wstring_view(className) == L"Edit" &&
                  (GetWindowLongW(nativeEditFrame(field), GWL_STYLE) & ES_READONLY) &&
                  !(GetWindowLongW(nativeEditFrame(field), GWL_STYLE) & WS_TABSTOP),
              "Input borders use a real native edit class without an extra editable/tab-stop input");
        RECT formatting{};
        SendMessageW(field, EM_GETRECT, 0, reinterpret_cast<LPARAM>(&formatting));
        const int line = nativeTextHeight(field);
        check(outer.bottom - outer.top >= line + 8 && inner.bottom - inner.top >= line,
              "Taller input frame and unclipped native text line");
        check(abs(inner.top + formatting.top + line / 2 - (outer.top + outer.bottom) / 2) <= 1,
              "Native edit formatting line is vertically centered inside the taller frame");
        check(!(GetWindowLongW(field, GWL_STYLE) & ES_MULTILINE),
              "Fields retain native single-line behavior");
    };
    check(controlText(app.hwnd_) == L"QuickStation", "Window branding");
    check(GetMenuItemCount(GetMenu(app.hwnd_)) == 3, "Native Stations / Options / Help menu bar");
    for (UINT i = 0; i < 3; ++i) {
        wchar_t caption[32]{};
        MENUITEMINFOW menuItem{sizeof(menuItem)};
        menuItem.fMask = MIIM_STRING | MIIM_FTYPE;
        menuItem.dwTypeData = caption;
        menuItem.cch = 32;
        check(GetMenuItemInfoW(GetMenu(app.hwnd_), i, TRUE, &menuItem) && caption[0] == L'&' &&
                  !(menuItem.fType & MFT_OWNERDRAW),
              "Top menu labels use default Windows drawing with accessible strings and mnemonics");
        const auto popup = GetSubMenu(GetMenu(app.hwnd_), i);
        MENUITEMINFOW popupItem{sizeof(popupItem)};
        popupItem.fMask = MIIM_FTYPE;
        check(GetMenuItemInfoW(popup, 0, TRUE, &popupItem) && !(popupItem.fType & MFT_OWNERDRAW),
              "Popup menus retain native drawing");
    }
    check(!(GetWindowLongW(app.control(Tabs), GWL_STYLE) & (TCS_FORCELABELLEFT | TCS_OWNERDRAWFIXED)),
          "Windows centers both tab labels with native drawing");
    check(GetMenuState(GetMenu(app.hwnd_), Find, MF_BYCOMMAND) != static_cast<UINT>(-1),
          "Browse command available in menu");
    check(GetMenuItemCount(GetSubMenu(GetMenu(app.hwnd_), 1)) == 2,
          "Options no longer exposes a manual engine recovery command");
    check(GetDlgItem(app.hwnd_, 213) == nullptr, "No extra menu separator control");
    BUTTON_IMAGELIST playImages{}, muteImages{};
    check(Button_GetImageList(app.control(Play), &playImages) && playImages.himl &&
              Button_GetImageList(app.control(Mute), &muteImages) && muteImages.himl,
          "Native playback buttons show icons alongside accessible text labels");
    const auto initialMuteImage = muteImages.himl;
    app.action(Mute);
    Button_GetImageList(app.control(Mute), &muteImages);
    check(muteImages.himl != initialMuteImage && controlText(app.control(Mute)) == L"Mute",
          "Speaker icon and label change together when mute state changes");
    app.action(Mute);
    const auto idleImages = playImages.himl;
    app.playback_.state = PlaybackState::Playing;
    app.updateDisplay();
    check(controlText(app.control(Play)) == L"Stop", "Single playback toggle shows Stop while playing");
    Button_GetImageList(app.control(Play), &playImages);
    check(playImages.himl != idleImages, "Play icon changes to Stop with playback state");
    app.action(Play);
    check(controlText(app.control(Play)) == L"Play" && app.audio_.snapshot().state == PlaybackState::Stopped,
          "Playback toggle stops the stream and returns to Play");
    check(GetParent(app.list_) == app.page_ && GetParent(app.page_) == app.control(Tabs),
          "Native tab containment");
    check(ListView_GetItemCount(app.list_) == 2, "Favorites-first list");
    check(!(ListView_GetExtendedListViewStyle(app.list_) & LVS_EX_CHECKBOXES), "Favorites has no checkboxes");
    check(ListView_GetImageList(app.list_, LVSIL_STATE) == nullptr, "Favorites has no checkbox image list");
    selectTab(false);
    check(ListView_GetItemCount(app.list_) == 3, "All stations retained");
    check((ListView_GetExtendedListViewStyle(app.list_) & LVS_EX_CHECKBOXES) != 0,
          "All stations has native checkboxes");
    check(ListView_GetCheckState(app.list_, 0) && !ListView_GetCheckState(app.list_, 2),
          "Saved favorite states shown");
    ListView_SetCheckState(app.list_, 2, TRUE);
    app.applyFavorites();
    check(app.library_.stations[2].favorite && app.audio_.snapshot().state == PlaybackState::Stopped,
          "Checkbox commits favorite without starting playback");
    selectTab(true);
    ListView_SetItemState(app.list_, 2, LVIS_SELECTED, LVIS_SELECTED);
    app.action(ToggleFavorite);
    check(ListView_GetItemCount(app.list_) == 2 && app.library_.stations.size() == 3,
          "Favorites context action removes favorite while retaining station");
    for (UINT dpi : {96u, 120u, 144u, 192u}) {
        app.dpi_ = dpi;
        app.fonts();
        app.resetLogos();
        app.rebuild();
        RECT outer{0, 0, app.px(660), app.px(510)};
        AdjustWindowRectExForDpi(&outer, WS_OVERLAPPEDWINDOW, TRUE, 0, GetDpiForWindow(app.hwnd_));
        SetWindowPos(app.hwnd_, nullptr, 0, 0, outer.right - outer.left, outer.bottom - outer.top,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        for (bool favorites : {true, false}) {
            selectTab(favorites);
            RECT tab{}, row{}, second{};
            TabCtrl_GetItemRect(app.control(Tabs), 0, &tab);
            check(tab.right - tab.left >= app.px(75) && tab.right - tab.left < app.px(146) &&
                      tab.bottom - tab.top >= app.px(30),
                  "Comfortable native tab targets");
            // Inspect Windows' own rendered glyphs, not a calculated text rectangle.
            // This catches asymmetric padding in selected and unselected native tabs.
            for (int index : {0, 1}) {
                RECT item{};
                TabCtrl_GetItemRect(app.control(Tabs), index, &item);
                BITMAPINFO info{};
                info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                info.bmiHeader.biWidth = item.right + 4;
                info.bmiHeader.biHeight = -(item.bottom + 4);
                info.bmiHeader.biPlanes = 1;
                info.bmiHeader.biBitCount = 32;
                void* pixels = nullptr;
                auto bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
                auto dc = CreateCompatibleDC(nullptr);
                if (!bitmap || !dc) {
                    if (bitmap)
                        DeleteObject(bitmap);
                    if (dc)
                        DeleteDC(dc);
                    throw std::runtime_error("Cannot inspect native tab rendering.");
                }
                auto old = SelectObject(dc, bitmap);
                RECT surface{0, 0, info.bmiHeader.biWidth, -info.bmiHeader.biHeight};
                FillRect(dc, &surface, GetSysColorBrush(COLOR_BTNFACE));
                SendMessageW(app.control(Tabs), WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT);
                GdiFlush();
                int first = item.right, last = item.left;
                int top = item.bottom, bottom = item.top;
                const auto* colors = static_cast<const DWORD*>(pixels);
                for (int y = item.top + 4; y < item.bottom - 4; ++y)
                    for (int x = item.left + 6; x < item.right - 6; ++x) {
                        const auto color = colors[y * surface.right + x];
                        if ((color & 255) < 80 && ((color >> 8) & 255) < 80 && ((color >> 16) & 255) < 80) {
                            first = std::min(first, x);
                            last = std::max(last, x);
                            top = std::min(top, y);
                            bottom = std::max(bottom, y);
                        }
                    }
                SelectObject(dc, old);
                DeleteDC(dc);
                DeleteObject(bitmap);
                check(first <= last, "Rendered native tab label is visible");
                if (dpi == GetDpiForWindow(app.control(Tabs)) &&
                    abs(first + last - item.left - item.right) > 6)
                    throw std::runtime_error("Native tab horizontal offset " +
                                             std::to_string(first + last - item.left - item.right) +
                                             " at DPI " + std::to_string(dpi));
                ++checks;
                // The loop also exercises simulated DPI geometry. Pixel centering is
                // meaningful only at the tab window's actual Windows DPI.
                if (dpi == GetDpiForWindow(app.control(Tabs)) &&
                    (top > bottom || abs(top + bottom - item.top - item.bottom) > 3))
                    throw std::runtime_error("Native tab vertical offset " +
                                             std::to_string(top + bottom - item.top - item.bottom) +
                                             " at DPI " + std::to_string(dpi));
                ++checks;
            }
            check(FindWindowExW(app.list_, nullptr, L"Static", nullptr) == nullptr,
                  "No added divider overlaps the native list header");
            check(!(ListView_GetExtendedListViewStyle(app.list_) & LVS_EX_GRIDLINES),
                  "Native gridlines cannot cover the lower themed selection edge");
            const auto page = rect(app.page_), list = rect(app.list_);
            check(list.left >= page.left && list.right <= page.right && list.top >= page.top &&
                      list.bottom <= page.bottom,
                  "Station list stays within tab page");
            ListView_GetItemRect(app.list_, 0, &row, LVIR_BOUNDS);
            ListView_GetItemRect(app.list_, 1, &second, LVIR_BOUNDS);
            check(second.top - row.top >= app.px(38) && second.top - row.top <= app.px(42),
                  "Native station rows have vertical spacing");
            RECT image{}, label{};
            ListView_GetItemRect(app.list_, 0, &image, LVIR_ICON);
            ListView_GetItemRect(app.list_, 0, &label, LVIR_LABEL);
            check(label.left >= image.right, "Station name follows padded logo cell");
            ListView_SetItemState(app.list_, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
            const auto logoMarker = nativeMarkerLogo(app.logos_.images(), app.px(UiSpacing::Logo),
                                                     UiSpacing::ListImageInset, app.px(UiSpacing::LabelGap));
            LVITEMW marker{};
            marker.mask = LVIF_IMAGE;
            marker.iImage = logoMarker;
            ListView_SetItem(app.list_, &marker);
            ListView_SetItemText(app.list_, 0, 1, const_cast<wchar_t*>(L"H"));
            ListView_SetItemText(app.list_, 0, 2, const_cast<wchar_t*>(L"H"));
            NativePixels rendered(app.list_);
            int logoLeft = rendered.width;
            for (int y = row.top; y < row.bottom; ++y)
                for (int x = image.left; x < image.right; ++x)
                    if (rendered.at(x, y) == 0xc020c0)
                        logoLeft = std::min(logoLeft, x);
            auto fontDC = GetDC(app.list_);
            auto oldFont = SelectObject(fontDC, app.font_);
            ABC bearing{};
            GetCharABCWidthsW(fontDC, L'H', L'H', &bearing);
            SelectObject(fontDC, oldFont);
            ReleaseDC(app.list_, fontDC);
            for (int column : {1, 2}) {
                RECT cell{};
                ListView_GetSubItemRect(app.list_, 0, column, LVIR_BOUNDS, &cell);
                int inkLeft = rendered.width;
                for (int y = row.top + 2; y < row.bottom - 2; ++y)
                    for (int x = cell.left + 1; x < cell.right - 1; ++x)
                        if (nativeDarkPixel(rendered.at(x, y)))
                            inkLeft = std::min(inkLeft, x);
                check(logoLeft < image.right && inkLeft < cell.right &&
                          abs(logoLeft - image.left + 4 - (inkLeft - cell.left - bearing.abcA)) <= 1,
                      "Rendered station logo and subitem text use matching native column insets");
            }
            if (!favorites)
                editFits(app.filter_);
            const auto mute = rect(app.control(Mute)), volume = rect(app.control(Volume)),
                       level = rect(app.control(Level));
            const auto icon = rect(app.control(PlayingIcon)), play = rect(app.control(Play)),
                       name = rect(app.control(PlayingName)), title = rect(app.control(PlayingTitle));
            check(icon.left == play.left + 1 && icon.left == list.left &&
                      icon.right - icon.left == app.px(UiSpacing::PlayingLogo),
                  "Visible logo, themed Play border and station list share a left edge");
            check(mute.left - play.right == app.px(UiSpacing::Gap) &&
                      volume.left - mute.right == app.px(UiSpacing::Gap) &&
                      level.left - volume.right == app.px(UiSpacing::Gap) &&
                      level.top == play.top && level.bottom == play.bottom,
                  "Meter aligns at the right of the playback control row");
            check(name.left == title.left &&
                      name.left == icon.left + app.px(UiSpacing::PlayingLogo + UiSpacing::Margin),
                  "Playback text shares a column and consistent gap after the visible logo");
            check(icon.right <= name.left,
                  "The native artwork control cannot cover station-name or metadata text");
            const auto group = rect(app.control(PlayingGroup));
            check(group.right - level.right == icon.left - group.left &&
                      volume.right == level.left - app.px(UiSpacing::Gap) &&
                      volume.right - volume.left > app.px(160),
                  "Volume fills available width before the inset audio meter");
            check(abs((name.top + title.bottom) - (icon.top + icon.bottom)) <= app.px(2) &&
                      abs(play.top - icon.bottom - app.px(UiSpacing::Section)) <= 1 &&
                      abs(group.bottom - play.bottom - app.px(UiSpacing::Section) - 2) <= 1,
                  "Artwork and two text lines center together with balanced group insets");
            const int topGap = icon.top - group.top - nativeTextHeight(app.control(PlayingGroup)) + 2;
            const int bottomGap = group.bottom - play.bottom - 2;
            check(abs(topGap - bottomGap) <= 1,
                  "Visible gap below the native group caption matches the gap below Play");
            int artworkWidth{}, artworkHeight{};
            ImageList_GetIconSize(app.playingLogos_.images(), &artworkWidth, &artworkHeight);
            check(artworkWidth == app.px(UiSpacing::PlayingLogo) &&
                      artworkHeight == app.px(UiSpacing::PlayingLogo),
                  "Now playing uses full-size artwork rather than the station-list thumbnail");
            app.playback_.stationId = app.library_.stations.front().id;
            app.playback_.state = PlaybackState::Playing;
            app.updateDisplay();
            const auto activeName = rect(app.control(PlayingName));
            check(GetDlgItem(app.hwnd_, 214) == nullptr &&
                      activeName.right == rect(app.control(PlayingTitle)).right,
                  "Station name has its full native line with no activity indicator control");
            const auto originalName = app.library_.stations.front().name;
            app.library_.stations.front().name.assign(256, 'W');
            app.updateDisplay();
            const auto longName = rect(app.control(PlayingName));
            check(sameRect(longName, activeName) &&
                      controlText(app.control(PlayingName)).size() == 256,
                  "Long station names ellipsize within the full native text line");
            app.library_.stations.front().name = originalName;
            app.playback_.state = PlaybackState::Stopped;
            app.updateDisplay();
            RECT track{}, thumb{};
            SendMessageW(app.control(Volume), TBM_GETCHANNELRECT, 0, reinterpret_cast<LPARAM>(&track));
            SendMessageW(app.control(Volume), TBM_GETTHUMBRECT, 0, reinterpret_cast<LPARAM>(&thumb));
            const int center = (mute.top + mute.bottom) / 2;
            check(abs(volume.top + (track.top + track.bottom) / 2 - center) <= 1,
                  "Volume track aligns with button center");
            check(abs(volume.top + (thumb.top + thumb.bottom) / 2 - center) <= 1,
                  "Native slider thumb aligns with button center");
            const auto slider = app.control(Volume);
            const int halfThumb = (thumb.right - thumb.left) / 2;
            const int travelLeft = track.left + halfThumb;
            const int travelRight = track.right - halfThumb;
            const int travelWidth = travelRight - travelLeft;
            check(travelWidth > 0, "Native volume slider has a usable channel");
            for (int wanted : {0, 20, 80, 100}) {
                SendMessageW(slider, TBM_SETPOS, TRUE, wanted < 50 ? 100 : 0);
                const int x = travelLeft + MulDiv(travelWidth, wanted, 100);
                const int y = (track.top + track.bottom) / 2;
                SendMessageW(slider, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
                SendMessageW(slider, WM_LBUTTONUP, 0, MAKELPARAM(x, y));
                const int actual = static_cast<int>(SendMessageW(slider, TBM_GETPOS, 0, 0));
                RECT movedThumb{};
                SendMessageW(slider, TBM_GETTHUMBRECT, 0, reinterpret_cast<LPARAM>(&movedThumb));
                check(abs(actual - wanted) <= 1 && app.library_.volume == actual,
                      "Clicking the volume channel selects the clicked level and updates playback volume");
                const int thumbCenter = (movedThumb.left + movedThumb.right) / 2;
                if (abs(thumbCenter - x) > app.px(3))
                    throw std::runtime_error("Native volume thumb misses channel click: DPI " +
                                             std::to_string(dpi) + ", wanted " + std::to_string(wanted) +
                                             ", clicked " + std::to_string(x) + ", thumb " +
                                             std::to_string(thumbCenter) + ", channel " +
                                             std::to_string(track.left) + ".." +
                                             std::to_string(track.right) + ", actual " +
                                             std::to_string(actual));
                ++checks;
            }
            SendMessageW(slider, TBM_SETPOS, TRUE, 50);
            SendMessageW(slider, TBM_GETTHUMBRECT, 0, reinterpret_cast<LPARAM>(&thumb));
            const int thumbX = (thumb.left + thumb.right) / 2;
            const int thumbY = (thumb.top + thumb.bottom) / 2;
            SendMessageW(slider, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(thumbX, thumbY));
            SendMessageW(slider, WM_MOUSEMOVE, MK_LBUTTON,
                         MAKELPARAM(travelLeft + MulDiv(travelWidth, 75, 100), thumbY));
            SendMessageW(slider, WM_LBUTTONUP, 0,
                         MAKELPARAM(travelLeft + MulDiv(travelWidth, 75, 100), thumbY));
            check(SendMessageW(slider, TBM_GETPOS, 0, 0) >= 70,
                  "Dragging the native volume thumb still works after channel-click handling");
            int iconWidth = 0, iconHeight = 0;
            Button_GetImageList(app.control(Play), &playImages);
            ImageList_GetIconSize(playImages.himl, &iconWidth, &iconHeight);
            check(iconWidth == app.px(16) && iconHeight == app.px(16),
                  "Button icons scale with native layout DPI");
            for (auto buttonControl : {app.control(Play), app.control(Mute)}) {
                BUTTON_IMAGELIST imageList{};
                Button_GetImageList(buttonControl, &imageList);
                check((GetWindowLongW(buttonControl, GWL_STYLE) & BS_LEFT) &&
                          imageList.margin.right <= app.px(UiSpacing::ButtonIconGap),
                      "Native button text stays beside its icon with the requested scaled gap");
            }
            for (bool active : {false, true}) {
                app.playback_.state = active ? PlaybackState::Playing : PlaybackState::Stopped;
                app.library_.muted = active;
                app.updateDisplay();
                for (auto buttonControl : {app.control(Play), app.control(Mute)}) {
                    NativePixels pixels(buttonControl);
                    int first = pixels.width, last = -1;
                    for (int y = app.px(6); y < pixels.height - app.px(6); ++y)
                        for (int x = app.px(5); x < pixels.width - app.px(5); ++x)
                            if (GetRValue(pixels.at(x, y)) + GetGValue(pixels.at(x, y)) +
                                    GetBValue(pixels.at(x, y)) <
                                3 * 180) {
                                first = std::min(first, x);
                                last = std::max(last, x);
                            }
                    check(last >= first && abs(first - (pixels.width - 1 - last)) <= app.px(2),
                          "Rendered native button icon/label group is horizontally centered");
                }
            }
            app.playback_.state = PlaybackState::Stopped;
            app.library_.muted = true;
            app.updateDisplay();
            int statusParts[4]{};
            check(SendMessageW(app.control(Status), SB_GETPARTS, 4, reinterpret_cast<LPARAM>(statusParts)) ==
                          4 &&
                      statusParts[0] == app.px(UiSpacing::Gap) &&
                      statusParts[2] - statusParts[1] == app.px(UiSpacing::Gap),
                  "Native status partitions provide equal scaled text insets");
            SendMessageW(app.control(Status), SB_SETTEXTW, ConnectionPart, reinterpret_cast<LPARAM>(L"H"));
            SendMessageW(app.control(Status), SB_SETTEXTW, SavePart,
                         reinterpret_cast<LPARAM>(L"H"));
            NativePixels statusPixels(app.control(Status));
            auto textInset = [&](int origin) {
                for (int x = origin; x < origin + app.px(30); ++x)
                    for (int y = 4; y < statusPixels.height - 3; ++y)
                        if (nativeDarkPixel(statusPixels.at(x, y)))
                            return x - origin;
                return -1;
            };
            const int connectionInset = textInset(0), saveInset = textInset(statusParts[1]);
            check(connectionInset >= app.px(UiSpacing::Gap) &&
                      connectionInset <= app.px(2 * UiSpacing::Gap) && abs(connectionInset - saveInset) <= 1,
                  "Both status messages have matching visible native text padding");
            app.updateDisplay();
            check(mute.right - mute.left >= nativeTextWidth(app.control(Mute), L"Unmute") + app.px(40),
                  "Button text and icon have room without clipping");
            ListView_SetItemState(app.list_, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            SetFocus(app.list_);
            SendMessageW(app.list_, WM_UPDATEUISTATE, MAKEWPARAM(UIS_SET, UISF_HIDEFOCUS), 0);
            check(!(SendMessageW(app.list_, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS),
                  "Native row focus outline remains visible after mouse focus changes");
            // The minimized message must not move any children into invalid geometry.
            const auto before = rect(app.control(PlayingName));
            SendMessageW(app.hwnd_, WM_SIZE, SIZE_MINIMIZED, 0);
            check(sameRect(before, rect(app.control(PlayingName))) && sameRect(list, rect(app.list_)),
                  "Minimized size notification leaves child geometry unchanged");
            SendMessageW(app.hwnd_, WM_SIZE, SIZE_RESTORED, 0);
            check(sameRect(before, rect(app.control(PlayingName))), "Restore returns to stable layout");
        }
        Editor editor{app.library_.stations[0]};
        auto dialog = CreateDialogParamW(instance, MAKEINTRESOURCEW(IDD_STATION), app.hwnd_, editProc,
                                         reinterpret_cast<LPARAM>(&editor));
        check(dialog != nullptr, "Station dialog creation");
        for (int id : {IDC_NAME, IDC_URL, IDC_LOGO, IDC_GENRE, IDC_NAME_LABEL, IDC_URL_LABEL, IDC_LOGO_LABEL,
                       IDC_GENRE_LABEL, IDC_FAVORITE, IDOK, IDCANCEL}) {
            SendDlgItemMessageW(dialog, id, WM_SETFONT, reinterpret_cast<WPARAM>(app.font_), FALSE);
        }
        layoutEditor(dialog, true);
        for (int id : {IDC_NAME, IDC_URL, IDC_LOGO, IDC_GENRE})
            editFits(GetDlgItem(dialog, id));
        check(GetNextDlgTabItem(dialog, GetDlgItem(dialog, IDC_NAME), FALSE) == GetDlgItem(dialog, IDC_URL),
              "Native input containers preserve dialog tab order");
        DestroyWindow(dialog);
    }
    selectTab(true);
    app.dpi_ = GetDpiForWindow(app.hwnd_);
    app.fonts();
    app.resetLogos();
    app.rebuild();
    app.layout();
    SetFocus(app.list_);
    ListView_SetItemState(app.list_, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    RECT selectedRow{}, client{};
    ListView_GetItemRect(app.list_, 0, &selectedRow, LVIR_BOUNDS);
    GetClientRect(app.list_, &client);
    BITMAPINFO pixelsInfo{};
    pixelsInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    pixelsInfo.bmiHeader.biWidth = client.right;
    pixelsInfo.bmiHeader.biHeight = -client.bottom;
    pixelsInfo.bmiHeader.biPlanes = 1;
    pixelsInfo.bmiHeader.biBitCount = 32;
    void* nativePixels{};
    auto rowBitmap = CreateDIBSection(nullptr, &pixelsInfo, DIB_RGB_COLORS, &nativePixels, nullptr, 0);
    auto rowDC = CreateCompatibleDC(nullptr);
    if (!rowBitmap || !rowDC)
        throw std::runtime_error("Cannot inspect native selection edge");
    auto previousBitmap = SelectObject(rowDC, rowBitmap);
    FillRect(rowDC, &client, GetSysColorBrush(COLOR_WINDOW));
    // Inspect the list's own renderer. WM_PRINT's child-header pass on Windows 11
    // is offset by the client edge and can cover the first two pixels of row zero.
    SendMessageW(app.list_, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(rowDC), PRF_CLIENT);
    GdiFlush();
    int edgePixels = 0;
    int blueEdges[4]{};
    const auto* colors = static_cast<const DWORD*>(nativePixels);
    // Inspect the native focus rectangle and the themed blue edge separately.
    for (int inset : {1, 2})
        for (int x = selectedRow.left + 50; x < std::min(selectedRow.right - 4, client.right); ++x) {
            const auto color = colors[(selectedRow.bottom - inset) * client.right + x];
            if ((color & 255) < 100 && ((color >> 8) & 255) < 100 && ((color >> 16) & 255) < 100)
                ++edgePixels;
        }
    auto blue = [](DWORD color) {
        const auto b = color & 255, g = (color >> 8) & 255, r = (color >> 16) & 255;
        return r < 200 && b > r + 30 && g > r + 10;
    };
    for (int inset : {0, 1}) {
        for (int x = selectedRow.left + 80; x < std::min(selectedRow.right - 4, client.right); ++x) {
            blueEdges[0] += blue(colors[(selectedRow.top + inset) * client.right + x]);
            blueEdges[1] += blue(colors[(selectedRow.bottom - 1 - inset) * client.right + x]);
        }
        for (int y = selectedRow.top + 4; y < selectedRow.bottom - 4; ++y) {
            blueEdges[2] += blue(colors[y * client.right + selectedRow.left + inset]);
            blueEdges[3] +=
                blue(colors[y * client.right + std::min(selectedRow.right, client.right) - 1 - inset]);
        }
    }
    const bool completeBlueBorder =
        std::all_of(std::begin(blueEdges), std::end(blueEdges), [](int count) { return count > 4; });
    if (!completeBlueBorder) {
        saveNativeBitmap(data / L"selection.bmp", pixelsInfo.bmiHeader, nativePixels);
    }
    SelectObject(rowDC, previousBitmap);
    DeleteDC(rowDC);
    DeleteObject(rowBitmap);
    check(edgePixels > 4, "Windows paints the bottom edge of the native selected-row focus outline");
    check(completeBlueBorder, "The native blue selection edge is visible on all four sides");
    checks += runBrowseUiChecks(instance, app.hwnd_, data / "browser-checks");
    // Warm both native checkbox/list variants before checking steady-state ownership.
    selectTab(false);
    selectTab(true);
    const auto handles = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    for (int i = 0; i < 20; ++i) {
        selectTab(false);
        selectTab(true);
    }
    check(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) <= handles + 2,
          "Repeated tab changes release native state image lists");
    const auto menuHandles = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    for (int i = 0; i < 20; ++i)
        SendMessageW(app.hwnd_, WM_THEMECHANGED, 0, 0);
    check(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) <= menuHandles + 2,
          "Repeated theme changes do not leak graphics resources");
    for (auto& station : app.library_.stations)
        station.favorite = false;
    app.refresh();
    check(ListView_GetItemCount(app.list_) == 0 &&
              (GetWindowLongW(app.control(Hint), GWL_STYLE) & WS_VISIBLE),
          "Empty favorites displays guidance after refresh");
    app.library_.stations.front().favorite = true;
    app.refresh();
    check(!(GetWindowLongW(app.control(Hint), GWL_STYLE) & WS_VISIBLE),
          "Favorites guidance disappears when a station is added");
    selectTab(false);
    for (bool doubleClick : {false, true}) {
        app.library_.doubleClick = doubleClick;
        app.refresh();
        check(!(GetWindowLongW(app.control(Hint), GWL_STYLE) & WS_VISIBLE),
              "Nonempty All stations has no permanent instructions for either playback preference");
        const auto fullList = rect(app.list_);
        applyFilter(L"no matching fixture station");
        check(ListView_GetItemCount(app.list_) == 0 &&
                  (GetWindowLongW(app.control(Hint), GWL_STYLE) & WS_VISIBLE) &&
                  controlText(app.control(Hint)).find(L"filter") != std::wstring::npos &&
                  rect(app.list_).top > fullList.top,
              "Filtering to no results shows relevant guidance and relays out the list");
        applyFilter(L"");
        check(ListView_GetItemCount(app.list_) == 3 &&
                  !(GetWindowLongW(app.control(Hint), GWL_STYLE) & WS_VISIBLE) &&
                  rect(app.list_).top == fullList.top,
              "Clearing the filter restores list space and hides empty guidance");
    }
    // Reordering uses the visible list, even when hidden stations are interleaved.
    app.library_.stations = {{"a", "Match A", "https://test/a", "", "", true},
                             {"x", "Hidden X", "https://test/x", "", "", false},
                             {"b", "Match B", "https://test/b", "", "", true},
                             {"y", "Hidden Y", "https://test/y", "", "", false},
                             {"c", "Match C", "https://test/c", "", "", true}};
    app.selectedId_.clear();
    selectTab(true);
    ListView_SetItemState(app.list_, 1, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    app.action(MoveDown);
    check(app.library_.stations[2].id == "c" && app.library_.stations[4].id == "b" &&
              app.library_.stations[1].id == "x" && app.library_.stations[3].id == "y" &&
              app.selected() && app.selected()->id == "b",
          "Favorites move swaps the next visible station while preserving hidden order and selection");
    app.action(MoveDown);
    check(app.library_.stations[4].id == "b", "Move at visible bottom is disabled");
    selectTab(false);
    applyFilter(L"Match");
    ListView_SetItemState(app.list_, 1, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    app.action(MoveUp);
    check(app.library_.stations[0].id == "c" && app.library_.stations[2].id == "a" &&
              app.selected() && app.selected()->id == "c" && app.store_.load().stations[0].id == "c",
          "Filtered move uses visible neighbor and persists order");
    POINT popup{};
    RECT target{};
    ListView_GetItemRect(app.list_, 2, &target, LVIR_BOUNDS);
    popup = {target.left + app.px(80), (target.top + target.bottom) / 2};
    ClientToScreen(app.list_, &popup);
    check(app.contextTarget(MAKELPARAM(popup.x, popup.y), popup) == 2 &&
              app.selected() && app.selected()->id == "b" &&
              app.audio_.snapshot().state == PlaybackState::Stopped,
          "Right-click selects the clicked row without starting playback");
    POINT keyboard{};
    check(app.contextTarget(-1, keyboard) == 2 && keyboard.y > popup.y,
          "Keyboard context menu anchors beside the focused station");
    RECT listClient{};
    GetClientRect(app.list_, &listClient);
    POINT blank{listClient.right - 2, listClient.bottom - 2};
    ClientToScreen(app.list_, &blank);
    check(app.contextTarget(MAKELPARAM(blank.x, blank.y), blank) == -1 &&
              app.selected() && app.selected()->id == "b",
          "Blank list space suppresses station actions without changing selection");
    applyFilter(L"");
    app.library_.stations.clear();
    app.library_.stations.reserve(MaxStations);
    for (size_t i = 0; i < MaxStations; ++i)
        app.library_.stations.push_back({"bench" + std::to_string(i), "Station " + std::to_string(i),
                                         "https://test/" + std::to_string(i), "", "", i % 2 == 0});
    app.refresh();
    const auto filterStart = Clock::now();
    SetWindowTextW(app.filter_, L"Station 49");
    const auto filterMs = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - filterStart);
    check(filterMs < std::chrono::milliseconds(30) && ListView_GetItemCount(app.list_) == 5000,
          "Typing in a full 5,000-station library does not rebuild the list on the keystroke");
    const auto rebuildStart = Clock::now();
    SendMessageW(app.hwnd_, WM_TIMER, FilterTimer, 0);
    const auto rebuildMs = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - rebuildStart);
    check(ListView_GetItemCount(app.list_) > 0 && rebuildMs < std::chrono::seconds(2),
          "Debounced filtering rebuilds accurate results for a full library");
    std::cout << "Filter 5,000 stations: input " << filterMs.count() << " ms, rebuild "
              << rebuildMs.count() << " ms\n";
    applyFilter(L"");
    // Exercise real persistence and Windows workspace-coordinate restoration.
    app.dpi_ = GetDpiForWindow(app.hwnd_);
    app.fonts();
    app.resetLogos();
    app.layout();
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromWindow(app.hwnd_, MONITOR_DEFAULTTONEAREST), &monitor);
    SetWindowPos(app.hwnd_, nullptr, monitor.rcWork.left + 30, monitor.rcWork.top + 40, 640, 470,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    WINDOWPLACEMENT original{sizeof(original)};
    check(GetWindowPlacement(app.hwnd_, &original) != FALSE, "Native window bounds can be captured");
    const auto settings = data / L"window.json";
    saveWindowPlacement(settings, original);
    auto restored = loadWindowPlacement(settings);
    check(restored && sameRect(restored->rcNormalPosition, original.rcNormalPosition) &&
              restored->showCmd == SW_SHOWNORMAL,
          "Window position and size persist independently of station data");
    SetWindowPos(app.hwnd_, nullptr, monitor.rcWork.left + 80, monitor.rcWork.top + 70, 730, 520,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    restored->showCmd = SW_HIDE;
    check(SetWindowPlacement(app.hwnd_, &*restored) != FALSE, "Windows applies saved placement");
    WINDOWPLACEMENT reapplied{sizeof(reapplied)};
    GetWindowPlacement(app.hwnd_, &reapplied);
    check(sameRect(reapplied.rcNormalPosition, original.rcNormalPosition),
          "Real native window returns to the saved position and size");
    original.showCmd = SW_SHOWMAXIMIZED;
    saveWindowPlacement(settings, original);
    check(loadWindowPlacement(settings)->showCmd == SW_SHOWMAXIMIZED, "Maximized window state persists");
    original.showCmd = SW_SHOWMINIMIZED;
    original.flags = WPF_RESTORETOMAXIMIZED;
    saveWindowPlacement(settings, original);
    check(loadWindowPlacement(settings)->showCmd == SW_SHOWMAXIMIZED,
          "Closing minimized from maximized reopens maximized");
    original.flags = 0;
    saveWindowPlacement(settings, original);
    check(loadWindowPlacement(settings)->showCmd == SW_SHOWNORMAL,
          "Closing minimized from normal reopens visibly at normal size");
    for (const auto* invalid :
         {"{broken}", "{\"version\":9}", "{\"version\":1,\"normal\":[1,2,3,4],\"maximized\":false}",
          "{\"version\":1,\"normal\":[0,0,999999999999,800],\"maximized\":false}"}) {
        atomicWrite(settings, invalid, false);
        check(!loadWindowPlacement(settings), "Damaged or invalid window settings safely use defaults");
    }
    original.showCmd = SW_HIDE;
    original.flags = 0;
    original.rcNormalPosition = {90000, 90000, 90640, 90470};
    saveWindowPlacement(settings, original);
    restored = loadWindowPlacement(settings);
    restored->showCmd = SW_HIDE;
    SetWindowPlacement(app.hwnd_, &*restored);
    GetWindowPlacement(app.hwnd_, &reapplied);
    check(reapplied.rcNormalPosition.left < 90000 && reapplied.rcNormalPosition.top < 90000,
          "Windows returns a saved off-screen window to an available display");
    SendMessageW(app.hwnd_, WM_CLOSE, 0, 0);
    check(!IsWindow(app.hwnd_) && loadWindowPlacement(settings).has_value(),
          "Normal quit saves window placement before destroying the app");
    const auto savedOnQuit = loadWindowPlacement(settings);
    {
        App reopened(data);
        reopened.openWindow(instance);
        WINDOWPLACEMENT startup{sizeof(startup)};
        GetWindowPlacement(reopened.hwnd_, &startup);
        check(sameRect(startup.rcNormalPosition, savedOnQuit->rcNormalPosition),
              "Actual app startup restores the geometry saved on quit");
        check(reopened.restoredShow_ == SW_SHOWNORMAL, "Actual app startup selects visible normal state");
    }
    auto maximized = *savedOnQuit;
    maximized.showCmd = SW_SHOWMAXIMIZED;
    saveWindowPlacement(settings, maximized);
    {
        App reopened(data);
        reopened.openWindow(instance);
        check(reopened.restoredShow_ == SW_SHOWMAXIMIZED,
              "Actual app startup remembers the maximized preference");
    }
    return checks;
}
