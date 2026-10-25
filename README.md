# QuickStation

A compact Windows 10/11 x64 desktop radio player written in C++20. Version 1.6.14 uses standard Win32 controls and Windows system colors. Favorites are the default view. Audio uses BASS, with BASSHLS for HLS streams.

Version 1.6.14 makes a click on the native volume slider's channel select the level under the pointer. Dragging the thumb and keyboard control remain native.

Version 1.6.13 gives the audio meter the same Windows push-button frame and height as its neighboring controls. The five level bars remain a passive display inside that frame. Windows theme APIs draw the frame when available, with classic Windows button drawing as a fallback.

Version 1.6.12 adds a compact audio-level meter at the far right of Now playing's control row. The volume slider expands to use the available space between Mute and the meter. Five bars track measured, post-volume audio peaks and clear when playback stops, buffers or is muted. Only this meter uses owner drawing; the surrounding controls retain Windows drawing.

Version 1.6.11 replaces the square badge with the approved transparent radio icon. The blue receiver fills the icon canvas, with a gold lightning antenna above it. The app window, taskbar and fallback station artwork use this icon; playback controls keep their own symbols. `design/make-logo.ps1` generates `assets/quickstation-logo.png` and its preview; `tools/make-icon.ps1` generates the multi-size Windows icon.

Version 1.6.10 uses the approved deep-plum, mint and coral QuickStation logo for the executable, title bar, taskbar and fallback station artwork. The native playback button symbols remain distinct. `design/make-logo.ps1` generates the source image at `assets/quickstation-logo.png`; `tools/make-icon.ps1` creates the multi-size Windows icon.

Version 1.6.9 removes the blinking circle beside the station name and returns the full native text line to the name. Long names shorten with an ellipsis at the available edge. The Now playing logo and visible Play button edge remain aligned.

Version 1.6.4 lowers the Now playing station name and song-title pair slightly to balance its visible text with the station artwork. The playback controls stay in place.

Version 1.6.3 uses the same eight-DIP gap between Play/Stop and Mute/Unmute and between Mute/Unmute and the volume slider.

Version 1.6.2 groups Play/Stop, Mute/Unmute and the volume slider from the left edge beneath Now playing. The space between each button icon and its text increases from five to nine DIP, scaling with display DPI.

Version 1.6.1 retains the most recent successful station search for seven days. If Radio Browser returns an error for that same search, the browser shows the recent results with an outage notice. If no matching cache exists, it reports the error and invites another attempt; saved stations and manual entry remain available.

Version 1.6.0 centers the native tab labels, reorders stations against the visible list, targets context menus to the clicked row, and delays large-library filtering until typing pauses. Persistent codec failures use a longer automatic retry interval; missing HLS plugins are diagnosed while ordinary streams remain playable. Failed logos retry after a cooldown, and the disk cache has background age, count and size limits. An Un4seen maintenance build of BASS fixes the reproduced long AAC startup for MONEY_893AAC on Windows 11 without an additional plugin; its provenance is in THIRD_PARTY_NOTICES.md.

Version 1.5.2 restores fully default Windows menu drawing, reduces station-row image cells to 38 DIP, centers compact playback-button icon/label groups, and adds equal native status-bar insets. All stations retains its local filter; instructions are in **Help → Using QuickStation** (F1), with contextual guidance shown only for empty lists. At that release, no owner drawing or custom control colors were used.

Reliability fixes enforce the same 8 MB limit when saving and loading, cancel obsolete directory requests at the socket, bound each HTTP request to 12 seconds, and report missing audio dependencies without a Windows loader dialog. Existing automatic playback recovery remains enabled.
## Run

Open `QuickStation.exe` from the extracted release folder (`dist/QuickStation-1.6.14` in this workspace). Keep `QuickStationAudio.exe`, `bass.dll`, and `basshls.dll` beside the executable. Extract the complete ZIP before running it. No installer or administrator rights are required. The target is Windows 10 version 1709 or later and Windows 11, x64. Windows 7/8 and 32-bit Windows are unsupported; the host and compatibility limits actually tested are recorded in VALIDATION.md.

- The main window remembers its position, size and maximized state when you quit. Closing while minimized reopens it visibly at its previous normal/maximized size. Windows adjusts an unavailable off-screen position to a current display.
- Click a station to listen. Enable **Double-click to play** in the **Options** menu if preferred.
- Use checkboxes in **All stations** to change favorites without starting playback. In **Favorites**, right-click a station to remove it from favorites. The station remains saved in All stations. Right-click also offers editing, reordering, and deleting a saved station.
- **All stations** includes a local filter. **Stations → Browse stations** opens a separate directory dialog with Country and Genre dropdowns, optional name search, and Station / Country / Genre result columns. The genre choices use up to 200 popular Radio Browser tags. You may browse without entering a name; up to 80 matching stations are returned, sorted by votes. Selected results can be added to favorites without interrupting playback.
- **Stations → Add by URL** accepts HTTP(S) streams and optional HTTP(S) logos.
- Playback recovery is automatic. Stop cancels playback and closes pending connections; activating another station replaces the previous attempt. Play/Stop and Mute/Unmute keep their text labels and show matching icons.
- Song titles appear when the station supplies ICY or Ogg metadata. Playback state appears in the station list and status bar.
- The single Play/Stop button disconnects the stream when stopped. Play reconnects to the remembered station's current live broadcast. This does not record or time-shift broadcasts.
- Import/export supports QuickStation JSON and UTF-8 M3U/M3U8 playlists. JSON preserves favorites and all station details. M3U preserves names, stream URLs and logo URLs; imported M3U entries become favorites. Import merges by exact stream URL and leaves existing entries unchanged.
- Shortcuts: Ctrl+F browse stations, Ctrl+N add, Ctrl+M mute, Ctrl+O import, Ctrl+E export, Ctrl+Space play/stop, F1 help. In All stations, Space toggles a favorite checkbox and Enter plays the selected station. Space in the filter types a normal space.

Four SomaFM channels are included as editable starter favorites. Station availability is controlled by each broadcaster.

## Local data

If Radio Browser is unavailable, browsing offers the most recent successful results for the exact same search, when they are less than seven days old. Otherwise it reports the error and can be retried. Radio Browser currently publishes only one active server, so its `all.api` and `de1.api` hostnames cannot overcome an outage of that server. Country/genre choices are reused in memory after a successful load. Existing stations play directly from their saved stream URLs, and manual entry, import/export, and local filtering remain available.

The last successful nonempty directory search, including its query, is stored in `logos/last-directory-search.cache` under the selected data folder. The next successful search replaces it. It is ignored after seven days.

Stations and preferences are in `%LOCALAPPDATA%\SmallRadio\stations.json`. The existing SmallRadio folder is deliberately retained so earlier installations keep their stations and settings. The previous saved version is `stations.json.bak`. Window placement is stored separately in `window.json`; missing or invalid window settings use the default layout. Station imports and exports leave your window placement alone. Cached logos are in `logos`. A small `playback.log` records connection/recovery events and numeric error codes; it omits station names, URLs, credentials, query tokens and song titles, and resets when it exceeds 64 KB. There is no account, database service, telemetry or cloud synchronization. Online discovery and station/logo streaming naturally require internet access.

To use a separate or portable data folder:

```powershell
.\QuickStation.exe --data-dir "D:\QuickStationData"
```

Only one process may use a data folder at a time. Writes use a flushed temporary file and atomic replacement. A damaged primary file is preserved before a valid backup is restored. If neither copy is valid, the app leaves both untouched and reports the problem.

## Build and test

Requires Visual Studio 2022, the Desktop development with C++ workload, Windows SDK and bundled CMake. Dependencies are vendored; normal builds do not download code.

```powershell
powershell -ExecutionPolicy Bypass -File tools\build.ps1
```

The script builds x64 Release, runs tests and assembles `dist/QuickStation-<version>` and `dist/QuickStation-windows-x64.zip`. Add `-Debug` for a debug build or `-SkipPackage` to build and test only. With `-SkipPackage`, `-Tests native_ui` runs a selected CTest expression; packaging always requires every suite. `-BuildDirectory build-1.6.14` uses a separate output directory while an older build is running. The C++ runtime is statically linked. `core_tests` covers data validation, Unicode, atomic saves, import and recovery. `audio_tests` runs a loopback radio server to exercise the real decoder, metadata, disconnect/reconnect, stop and rapid station changes without internet or audible output. It also checks withheld headers/audio, slow startup, stalled HLS segments, real socket closure, frozen/crashed decoder recovery, bounded shutdown and private bounded diagnostics. Recovery cases exercise repeated failures, temporary non-audio content, cancellation during backoff, and helper files becoming available after a launch failure, without a manual engine restart. `logo_tests` checks the Windows image decoder, malformed and oversized images, bounded thumbnail memory, and graphics-handle cleanup during repeated DPI changes. `native_ui_tests` exercises the actual hidden native controls, default menu types/captions/mnemonics, Favorites changes, the Play/Stop toggle, button image/label changes, rendered button group centering, icon DPI sizing, rendered column insets, all four blue selection edges, removal of added dividers/gridlines, native field centering/tab order, browse-dialog bounds, resource cleanup and minimized-size notifications with 100%, 125%, 150% and 200% font/layout metrics. `directory_tests` checks filter encoding, malformed/oversized/deep responses, bounded results, deduplication, real loopback HTTP requests, failover, cancellation, empty results and offline errors.

Packaging also runs `tools/release-smoke.ps1`: it extracts the ZIP, compares every file with the release folder by SHA-256, then starts and closes that extracted copy using a temporary data profile. The gate requires the first station file and window settings to be saved, and removes its temporary files afterward. This checks the portable package on the build host; it does not replace testing on another Windows installation.

## Design and reliability

- A lightweight supervisor thread owns at most one `QuickStationAudio.exe` process. All BASS/codec/driver calls run there. Anonymous inherited handles carry requests and snapshots; URLs are not exposed in the process command line. Command generations discard obsolete results. A Windows job object closes the helper if the main process exits.
- Each connection attempt has a 15-second total startup deadline, covering initialization, playlists, HLS and decoder probing, in addition to 7-second network-operation limits. After playback begins, 5 seconds without worker progress triggers recovery; a stream buffering for 12 seconds is recreated. Stop and station switching first allow a short graceful exit, then terminate only the disposable helper if necessary. A replacement is launched only after the previous process has exited. Reconnect delay grows from 1 to 32 seconds and resets after 30 seconds of stable playback. Temporary helper-launch failures and non-audio/codec-probing responses are retried automatically. Authorization failures and invalid/unsupported protocols report an actionable error; another station or a fresh Play attempt remains available. The supervisor stays alive after recoverable exceptions.
- Network discovery and logos run in a background worker using cancellable asynchronous WinHTTP, with size limits and a 12-second total deadline per request, including response headers and body. A new search preempts older searches and background downloads. Shutdown cancels pending I/O. Callback state and read buffers remain owned until Windows finishes closing the request. Downloads do not bypass TLS verification. Logo downloads and existing cache files are validated with WIC. Temporary failures receive up to three attempts with bounded backoff; invalid cache files are discarded. Absent, broken or unsupported provider images retain the application-icon fallback.
- JSON documents are bounded to 8 MB, 5,000 stations and 32 nesting levels. Save and export operations enforce the corresponding read limits before replacing any file. Imports are validated before committing. URLs cannot inject custom HTTP headers.
- Resource handles use scoped ownership. The supervisor never waits inside decoder code or forcibly terminates a thread. It briefly waits for helper exit and uses the job object as a final cleanup backstop. If Windows has not completed process termination, recovery reports that state and retains ownership instead of accumulating replacement processes.
- Tabs, lists, checkboxes, buttons, text fields, sliders, status bars and dialogs use standard Windows controls. A common-controls v6 manifest enables the system's visual styles. The menu bar and popup menus also use fully default Windows drawing; there are no owner-drawn UI controls. There is no custom dark palette, forced title-bar color, GDI+ dependency or third-party UI framework. Windows Imaging Component decodes station logos for a native image list.
- Native controls retain keyboard focus and accessible labels. Taller input frames use standard EDIT controls with Windows system brushes; Windows still paints the border, text, caret, and selection. Fields keep single-line editing and native dialog tab order. Per-monitor DPI scaling is supported. Volume and playback preferences are saved after a short debounce; save failures are reported and retried when closing.

No implementation can promise absolute reliability across all networks, codecs and audio drivers. The included automated tests cover reproducible failures; real station availability and long-running operation still depend on the environment.

## Format support and licensing

BASS supports common MP3, Ogg and WAV streams, with AAC through Windows Media Foundation codecs. BASSHLS adds HLS. Other formats may require additional codecs/plugins. Station metadata is optional and varies by protocol; not all HLS timed metadata is exposed by this version.

BASS/BASSHLS are third-party binary libraries, free for qualifying non-commercial use. Commercial use requires the appropriate Un4seen license. See `THIRD_PARTY_NOTICES.md` and the bundled license documentation before commercial distribution.

