# QuickStation validation

## 1.6.14 release on 2026-10-23

Windows' default trackbar channel click moved the volume by a page, so the thumb could stop before or after the pointer. QuickStation now maps a channel click to the native thumb's actual travel range, including the half-thumb inset at each end. Clicking the thumb still starts Windows' normal drag; keyboard control and native drawing remain unchanged. The native UI suite checks channel clicks near both ends and in the middle, thumb placement, volume-model updates and native dragging across 100%, 125%, 150% and 200% layout metrics.

The x64 Release build passed all five suites without compiler warnings: **739 checks** (47 core, 67 audio, 11 logos, 573 native UI, 41 directory) in 91.48 seconds. Evidence: `build-1.6.14/Testing/Temporary/LastTest.log`. All ten ZIP entries matched the release folder by SHA-256; the extracted app started with an isolated profile, saved its first station file and window state, and closed normally. Release files are in `dist/QuickStation-1.6.14` and `dist/QuickStation-windows-x64.zip`.

## 1.6.13 release on 2026-10-16

The meter's previous recessed `DrawEdge` frame looked older than the adjacent Play and Mute buttons. It now uses the same Windows push-button theme part in its normal state. The five audio bars stay inside the theme's content rectangle; classic Windows button drawing is used if the theme is unavailable. The meter remains passive and matches the buttons' height and row alignment.

The x64 Release build passed all five suites without compiler warnings: **659 checks** (47 core, 67 audio, 11 logos, 493 native UI, 41 directory) in 98.17 seconds. The first sandbox run could not complete the local loopback audio/directory fixtures; rerunning the same binaries with host permissions passed all five suites. Evidence: `build-1.6.13/Testing/Temporary/LastTest.log`. Release files are in `dist/QuickStation-1.6.13` and `dist/QuickStation-windows-x64.zip`.

Release verification follow-up: the ZIP now has an automated extracted-package smoke gate in `tools/release-smoke.ps1`. It checks all ten file hashes, starts the extracted application with an isolated profile, confirms first-run station storage, closes normally, confirms window-state storage and removes the temporary profile. The standalone gate passed on this Windows 11 build host. A separate clean Windows 10/11 installation, actual high contrast and mixed physical DPIs, device unplugging, and extended listening remain outside this host's validation.

Repository cleanup on 2026-10-16 removed the superseded 1.6.11–1.6.12 build trees and 1.6.10–1.6.12 release folders: 495 files, approximately 48.23 MiB. The 1.6.12 test log was copied and hash-verified at `backups/validation-1.6-history/1.6.12-LastTest.log`. The current 1.6.13 build, release folder and ZIP, source, vendored libraries, validation backups, and `test-data/native-ui-152` saved profile were retained. No QuickStation process used a removed folder. The documentation-only package refresh passed the extracted-ZIP smoke gate again.

## 1.6.12 release on 2026-10-15

The Now playing control row has a small recessed level meter at its far right. The native volume slider fills the width between Mute and the meter at each window size and DPI. The meter is the only owner-drawn control: five bars use Windows system colors and follow the decoder's peak level scaled by QuickStation's own volume. It clears on mute, stop, buffering and reconnect. The private helper IPC version is 5 so mismatched audio helpers cannot read the new report field.

The x64 Release build passed all five suites without compiler warnings: **659 checks** (47 core, 67 audio, 11 logos, 493 native UI, 41 directory) in 89.94 seconds. The audio suite uses a non-audible local tone fixture to check real level reporting, mute and stop. The native UI suite checks the meter's right inset and the slider's dynamic width at its tested DPI scales. Evidence is archived at `backups/validation-1.6-history/1.6.12-LastTest.log`. At release time, all ten ZIP entries matched the 1.6.12 release folder by SHA-256.

## 1.6.11 release on 2026-10-15

The approved transparent blue radio with gold lightning antenna replaces the dark square badge. The 512-pixel source matches the approved proposal byte-for-byte, has a fully transparent corner, and generates 16, 20, 24, 32, 48, 64, 128 and 256-pixel ICO frames. `design/make-logo.ps1` now regenerates this source and its preview.

The x64 Release build passed all five suites without compiler warnings: **649 checks** (47 core, 65 audio, 11 logos, 485 native UI, 41 directory) in 104.00 seconds. The original 1.6.11 test log was overwritten during 1.6.12 development; these figures are the contemporaneous release record. Windows extracted a 32-pixel icon from the built executable with a transparent corner, and the executable metadata reported product and file version 1.6.11. Its release folder was retained temporarily for rollback and removed in the 2026-10-16 cleanup.

The superseded 1.6.10 build tree was removed after preserving its test log. Windows initially denied removal of the old release executables while they were in use, so the complete 1.6.10 folder was kept until the application exited. It was removed in the 2026-10-16 cleanup.

## 1.6.10 release on 2026-10-13

The approved deep-plum, mint and coral logo is now the executable icon, the main-window title-bar/taskbar icon and the fallback station artwork. `assets/quickstation-logo.png` is the 512-pixel source; `tools/make-icon.ps1` generates 16, 20, 24, 32, 48, 64, 128 and 256-pixel icon frames. Playback button symbols are unchanged.

The x64 Release build passed all five suites without compiler warnings: **649 checks** (47 core, 65 audio, 11 logos, 485 native UI, 41 directory) in 117.30 seconds. Evidence: `backups/validation-1.6-history/1.6.10-LastTest.log`. Windows extracted the new 32-pixel icon from the built executable; its center pixel is mint (`#58EBCA`). The executable metadata reports product and file version 1.6.10. Release files are in `dist/QuickStation-1.6.10` and `dist/QuickStation-windows-x64.zip`.

Repository cleanup removed ten superseded 1.6.0–1.6.9 build trees and eleven 1.5.3–1.6.9 release folders. Their small validation logs were hash-verified and preserved under `backups/validation-1.6-history`; older release paths below record their location at release time. The current build and package remain. The old blue logo asset and duplicate proposal icon were removed; `design/make-logo.ps1` regenerates the approved source without changing its bytes. The `test-data/native-ui-152` profile remains because it contains station changes made during use.

## 1.6.9 release on 2026-10-13

The activity indicator is gone. The station name uses the full native text line again, and playback state remains available in the station list and status bar. The obsolete audio-level sampling, public getter, private IPC field, and tone-only test fixture were removed. The IPC version is now 4 so mismatched helper copies cannot interpret the changed shared report layout. A separate deep-plum, mint and coral logo proposal is available in `design/quickstation-logo-proposal.png`; the installed icon remains unchanged.

The x64 Release build passed all five suites without compiler warnings: **649 checks** (47 core, 65 audio, 11 logos, 485 native UI, 41 directory) in 114.64 seconds. Evidence: `backups/validation-1.6-history/1.6.9-LastTest.log`. Release files are in `dist/QuickStation-1.6.9` and `dist/QuickStation-windows-x64.zip`. The invalid-output-device test now waits up to 12 seconds under a loaded host, still below the player's 15-second startup limit, and reports the observed status on failure.

## 1.6.8 release on 2026-10-13

The Now playing control row shifts one physical pixel left so the visible themed Play border aligns with the logo edge shown in the user's screenshot. The changing-size audio dot is replaced with a native static-control circle two DIP after the station-name control. It keeps one shape and size, blinking on/off every 500 ms while playback is Playing; it hides in other states. No owner drawing is used for this indicator. The native UI suite checks placement, rendered circular bounds, and blink transitions across 100%, 125%, 150% and 200% layout scales.

The x64 Release build passed all five suites without compiler warnings: **685 checks** (47 core, 69 audio, 11 logos, 517 native UI, 41 directory) in 97.34 seconds. Evidence: `backups/validation-1.6-history/1.6.8-LastTest.log`. Release files are in `dist/QuickStation-1.6.8` and `dist/QuickStation-windows-x64.zip`. An earlier full run timed out on the audio suite's invalid-output-device status assertion; the audio suite passed on retry, then passed again in the final full run without source changes.

## 1.6.7 release on 2026-10-13

The user-marked 1.6.5 screenshot measured 22 pixels between the Now playing caption and artwork and 18 pixels between the playback button and the bottom border. The four-DIP change in 1.6.6 shifted the balance too far. Version 1.6.7 moves the artwork, text, buttons and slider together only two screen pixels above their 1.6.5 positions, yielding matching 20-pixel visible gaps at the screenshot's display scale. Group height, logo/text centering, and the gap between artwork and controls remain unchanged. A native UI regression check compares the visible group insets at 100%, 125%, 150% and 200% layout scales.

The x64 Release build passed all five suites without compiler warnings: **669 checks** (47 core, 69 audio, 11 logos, 501 native UI, 41 directory) in 110.38 seconds. Evidence: `backups/validation-1.6-history/1.6.7-LastTest.log`. Release files are in `dist/QuickStation-1.6.7` and `dist/QuickStation-windows-x64.zip`.

## 1.6.6 release on 2026-10-13

The Now playing artwork, both text lines, playback buttons and volume slider move up together by four DIP inside the same 140-DIP group. This reduces the visible gap above the logo and increases the gap below the buttons, while preserving the sixteen-DIP separation between the artwork and controls. The native UI suite checks their alignment and spacing at 100%, 125%, 150% and 200% layout scales.

The x64 Release build passed all five suites without compiler warnings: **661 checks** (47 core, 69 audio, 11 logos, 493 native UI, 41 directory) in 100.57 seconds. Evidence: `backups/validation-1.6-history/1.6.6-LastTest.log`. Release files are in `dist/QuickStation-1.6.6` and `dist/QuickStation-windows-x64.zip`.

## 1.6.5 release on 2026-10-13

Now playing uses a separate 44-DIP artwork image, vertically centered with the station-name and song-information lines. The group has a sixteen-DIP gap between artwork and playback controls and the same inset below the buttons. The station title keeps a small native text control with end ellipsis when necessary, reserving space for the audio dot immediately after the visible title. The controls and slider remain native and keep their previous horizontal spacing.

The supervised audio helper samples mono RMS with BASS's volume/pan adjustment and passes a bounded level through its private IPC report. The UI smooths that value into the dot's size, using Windows system colors; the dot dims with silence or mute and disappears outside Playing. This reports decoded stream activity after QuickStation's volume setting, not physical speaker output. The only new custom drawing is the dot.

The x64 Release build passed all five suites without compiler warnings: **661 checks** (47 core, 69 audio, 11 logos, 493 native UI, 41 directory) in 96.17 seconds. The audio suite checks real decoded level, mute, unmute and Stop against a loopback tone stream. The native UI suite checks logo/text/control balance, title truncation, dot placement/rendering and stopped visibility across 100%, 125%, 150% and 200% layout metrics. Evidence: `backups/validation-1.6-history/1.6.5-LastTest.log`. Release files are in `dist/QuickStation-1.6.5` and `dist/QuickStation-windows-x64.zip`.

All ten release-folder files matched their ZIP entries by SHA-256, and the packaged main EXE matched the tested build. The ZIP-extracted application launched with a separate test profile and created its station file; that process and profile were removed afterward. Actual high-contrast mode and physical mixed-DPI monitors remain environment checks, as noted below.

## 1.6.4 release on 2026-10-12

The Now playing station name and song-title controls sit three DIP lower to balance their visible glyphs against the logo. The native artwork, buttons and volume slider keep their positions. The x64 Release build passed all five suites without compiler warnings: **609 checks** (47 core, 65 audio, 11 logos, 445 native UI, 41 directory) in 103.56 seconds. Evidence: `backups/validation-1.6-history/1.6.4-LastTest.log`. Release files are in `dist/QuickStation-1.6.4` and `dist/QuickStation-windows-x64.zip`.

## 1.6.3 release on 2026-10-12

The volume slider now starts eight DIP after Mute/Unmute, matching the eight-DIP gap between Play/Stop and Mute/Unmute. The native UI suite verifies both gaps and that the slider remains inside the station list's right edge across tested display scales. The x64 Release build passed all five suites without compiler warnings: **609 checks** (47 core, 65 audio, 11 logos, 445 native UI, 41 directory) in 99.70 seconds. Evidence: `backups/validation-1.6-history/1.6.3-LastTest.log`. Release files are in `dist/QuickStation-1.6.3` and `dist/QuickStation-windows-x64.zip`.

## 1.6.2 release on 2026-10-12

The Now playing controls follow the supplied reference: Play/Stop starts at the same left edge as the station list and artwork, Mute/Unmute follows with an eight-DIP gap, and the native volume slider follows with a sixteen-DIP gap. Button icon-to-text spacing increased from five to nine DIP (four pixels more at 100% scaling). Native image-list margins and measured button widths use the same gap, retaining centered icon/label groups at 100%, 125%, 150% and 200% layout metrics. No custom button or slider painting was added.

The first full run compiled successfully, but the native UI suite exceeded its 90-second test timeout under host load. That suite passed in isolation with **445 checks**. Its release timeout is now 150 seconds. The final x64 Release run passed all five suites without compiler warnings: **609 checks** (47 core, 65 audio, 11 logos, 445 native UI, 41 directory) in 156.09 seconds. Evidence is `backups/validation-1.6-history/1.6.2-LastTest.log`. The build script now compares vendor DLL hashes before replacing them, allowing an identical in-use DLL to remain while tests run; changed DLLs still have to be copied and tested. The running build copy was not closed.

Release files are in `dist/QuickStation-1.6.2` and `dist/QuickStation-windows-x64.zip`.

## 1.6.1 release on 2026-10-11

The user reported an HTTP 503 while browsing for Bloomberg. The service was available during investigation: direct requests returned HTTP 200, and QuickStation's own WinHTTP path loaded 241 countries, 200 genres and 16 Bloomberg stations. Current DNS resolves `all.api.radio-browser.info` and `de1.api.radio-browser.info` to the same server; `/json/servers` lists only that server. The application's failover cannot provide fresh results during an outage of this upstream service.

The browser now saves only its most recent nonempty successful search in the local cache. If an identical search fails within seven days, it shows those results with an outage notice. Other queries, expired or corrupt cache files, and first searches report the server error. Cache write failures do not break a successful search. Saved station playback and Add by URL remain independent of the directory. Local HTTP tests cover server failure, cache reuse across restart, query isolation, expiry and corruption.

The x64 Release build passed all five suites without compiler warnings: **601 checks** (47 core, 65 audio, 11 logos, 437 native UI, 41 directory) in 140.33 seconds. Evidence is `backups/validation-1.6-history/1.6.1-LastTest.log`. The release package is `dist/QuickStation-1.6.1` and `dist/QuickStation-windows-x64.zip`. A live service success today does not establish future Radio Browser availability; an independent mirror was not published at the time of this check.

## 1.6.0 release on 2026-10-11

Built x64 Release with MSVC 19.44 and Windows SDK 10.0.26100, without compiler warnings. All five suites passed against the bundled Un4seen maintenance DLL: **596 checks** (47 core, 65 audio, 11 logos, 437 native UI, 36 directory) in 115.70 seconds. Evidence: `backups/validation-1.6-history/1.6.0-vendor-release-check.log` and `backups/validation-1.6-history/1.6.0-LastTest.log`. The native UI suite renders both tab states at the real Windows DPI and checks layout at 100%, 125%, 150% and 200% metrics. A 5,000-station filter input measured under 1 ms on this run; the debounced rebuild measured 409 ms under machine load. These timings are host-specific.

The user-supplied MONEY_893AAC URL returned HTTP 200, `audio/aacp`, and 64 KiB within 1.9 seconds. The previously bundled BASS DLL remained in Connecting until QuickStation's 15-second watchdog ended the attempt. The vendor maintenance DLL reached Playing in about 3.5 seconds and remained there for a 30-second muted playback probe without a reconnect. It is 2,360 bytes larger than the prior DLL. The URL and its query parameters are absent from the source, private diagnostics and package; only its station name appears in this record. The vendor build's exact origin and hashes are in THIRD_PARTY_NOTICES.md. It is a maintenance build, not yet a numbered public release.

Regression coverage now includes persistent format-error cooldown and immediate Stop/switch/Play, audio initialization failures, missing/invalid HLS plugins, visible-neighbor reordering and right-click targeting, logo retry after exhaustion, bounded cache cleanup, and large-library filter input. No decoder change can guarantee every stream or server; broader AAC/HLS fixtures, clean Windows machines, mixed physical DPIs and an independently demonstrated expiring Radio Browser URL remain unverified in TODO.md.

The package build repeated the five suites successfully in 135.62 seconds (`backups/validation-1.6-history/1.6.0-release-build.log`). All ten release-folder files matched their ZIP entries by SHA-256; both EXEs and both DLLs matched the tested build, including the vendor BASS hash. The ZIP-extracted app started with a separate temporary data folder. Its test process and profile were removed afterward, and the user's running 1.5.3 copy and saved stations were untouched. The obsolete 1.5.3 build was removed after its logs were archived; the 1.5.3 release folder remains for rollback.

## Previous 1.5.3 release

## Spacing follow-up on 2026-10-10

Increased the shared playback-button icon/text gap from four to five DIP. Button widths and image-list margins use that same value, retaining balanced native centering in Play, Stop, Mute and Unmute states. The initial image-list margin now also uses the shared constant. All UI controls remain Windows-drawn.

Built x64 Release without compiler warnings and passed all five suites again: **542 checks** (47 core, 54 audio, 11 logos, 397 native UI, 33 directory), in 82.66 seconds. The native UI suite checks rendered button centering at 100%, 125%, 150% and 200% metrics. Evidence: `backups/validation-1.5.3/release-build.log`; the original per-test log was overwritten by later 1.6.0 development runs. Packaged binaries were compared against the tested build, and ZIP entries were verified by SHA-256. The previous release was initially retained while running and removed in the subsequent user-requested cleanup. The earlier interactive observations below apply to 1.5.2; no new interactive listening run was needed for this spacing-only change. Compatibility limits below still apply.

## Previous 1.5.2 release evidence

Release built and tested on 2026-10-09; final extracted-package inspection and cleanup completed on 2026-10-10. C++20, MSVC 19.44, Windows SDK 10.0.26100, x64 Release with the static C++ runtime. Available host: Windows 11 25H2, build 26200.9457. The former `build-1.5.2` directory was removed after the 1.5.3 release. Its test and build logs are preserved as `backups/validation-1.5.2/LastTest.log` and `backups/validation-1.5.2/release-build.log`.

## Changes and audit findings

- Restored the original HMENU to default Windows drawing. Removed the menu helper, owner measurement/painting, nonclient fills, mnemonic workaround and associated tests/build entries. Windows owns menu origins, spacing, fonts, colors, hover and keyboard behavior.
- Main station image cells use 32-DIP logos with three DIP above/below. Native Explorer full-row selection remains intact, without added gridlines or header dividers.
- Playback buttons keep stable, individually measured Play/Stop and Mute/Unmute widths. Native image-list margins center each visible icon/label group using shared spacing and known ordinary-asset bounds. Rendering checks include antialiased glyph pixels, which are necessary for accurate text bounds. No custom button painting.
- Empty borderless native status parts supply eight-DIP insets. Native status drawing, height, separator and grip remain. Connection text has a native overflow tooltip. No strings padded with spaces and no status owner drawing.
- All stations retains its local filter and reclaims the permanent instruction line. Help → Using QuickStation (F1) reflects the current activation preference. Empty Favorites and no-match filters have appropriate guidance; filtering now relays out the list when that guidance appears or disappears. Hidden guidance is cleared so it cannot become a misleading accessibility label.
- Found and fixed an asymmetric persistence limit: a valid large library could exceed the reader's 8 MB maximum when encoded. Saves and exports now reject oversize output before replacing the destination; tests verify that both the existing library and backup survive.
- Directory cancellation previously discarded results while synchronous HTTP continued. WinHTTP requests now use asynchronous completion with a 12-second total deadline covering headers/body and cancellation polled at 50 ms. Search replacement preempts older requests and background downloads; interrupted logos do not consume retries. Callback context and buffers remain alive through HANDLE_CLOSING, following Microsoft's documented lifecycle. No detached application threads or per-request thread accumulation.
- Missing decoder DLLs now give an actionable recoverable status. The main process sets loader error mode before launching children, suppressing blocking loader dialogs. Tests exercise a missing helper followed by a missing BASS DLL, then automatic recovery when the complete files appear. Unavailable output-device errors have understandable messages.
- On-disk logo size is checked before decoding on the UI thread, in addition to existing dimension/memory bounds. Temporary bitmap allocation order no longer leaks a GDI handle if the pixel-buffer allocation throws.
- Reviewed audio generations, startup/progress/stall deadlines, helper job/process ownership, bounded shutdown, stale result rejection, retry backoff, local data recovery, metadata/input bounds, logo cache/retries and message callback exception boundaries. Existing deterministic regression coverage was retained. Shared control-text and CMake source lists replace duplication; obsolete menu code and preview generation were removed.

## Test coverage

All five suites passed: **542 checks** (47 core, 54 audio, 11 logos, 397 native UI, 33 directory). The full release run took 82.48 seconds, with no compiler warnings. The suites exercise actual production code and native controls with isolated data:

- Core: validation, Unicode, malformed/deep/oversized JSON, M3U/JSON round trips, atomic saves, locked-file failures, backup recovery and preservation, save/export size symmetry.
- Audio: real silent-device decoding on loopback WAV/ICY fixtures, metadata, network loss/reconnect, repeated failures, temporary non-audio responses, rapid station switching, stop during connection/backoff, withheld headers/audio, dribbling startup and stalled HLS, process freeze/crash, real socket closure, missing helper/DLL recovery and bounded private logs.
- Logos: valid/malformed/missing images, cached-image repair, dimension and file-size limits, bounded thumbnail memory and graphics-handle stability.
- Native UI: default menu/captions, native tab containment, checkboxes only in All stations, both playback preferences, empty/nonempty/filter transitions, rendered group centering and status insets, four button states, native field/label centering, column/logo insets, complete selection edges, minimize/restore, window persistence/off-screen placement, Browse, repeated view/theme changes and graphics-resource ownership. Layout/font checks use 100%, 125%, 150% and 200% metrics.
- Directory: real loopback HTTP failover, offline errors, validated bounded results, invalid/deep/oversized data, URL query encoding, cache repair/retries, actual stale socket cancellation, responsive shutdown with withheld headers, continuously dribbling responses stopped by the total deadline, and successful requests after errors.

The live HTTPS directory check returned 241 countries and 200 genres. PE import inspection found no MSVCP/VCRUNTIME runtime DLL dependency in either application executable; QuickStationAudio requires the bundled bass.dll. Application manifest requests ordinary user privileges.

## Interactive and package checks

Inspected the production app's default menu, Favorites/All stations views, Help (F1), empty-filter guidance and restoration of the list after clearing the filter. Live BBC World Service playback supplied its title metadata. During an earlier BFM Business listening check, the stream repeatedly disconnected; automatic retry worked, and switching to BBC during retry immediately replaced the old attempt. This verifies observed recovery and switching, not uninterrupted availability of that broadcaster.

Extracted the release ZIP into a separate workspace directory and compared every packaged file against the release folder with SHA-256. Both executables and decoder DLLs also matched the tested build. The extracted app launched using a separate muted data profile, played BBC World Service with metadata, and restored after minimizing while remaining connected. The restored window showed no duplicate text or stale control outlines. Normal close exited both application and audio helper and wrote window placement. The temporary extraction was removed afterward; its playback log and placement evidence remain in `backups/validation-1.5.2/package-verification`. The main executable is 777,728 bytes.

## Compatibility limits

The supported target is Windows 10 version 1709 or later and Windows 11, x64. The available host was tested; a separate clean Windows 10/11 machine without developer tools was not available. Static-runtime/import inspection supports packaging independence but is not a substitute for that compatibility run. Windows 7/8, 32-bit Windows and unsupported CPU/OS configurations are outside the target.

Actual OS high contrast, every theme/font combination, unplugged/no-device audio, physical mixed-DPI monitor moves, every AAC/HLS codec variant, suspend/resume and multi-day listening have not been exhaustively exercised. Native layout tests simulate scaled fonts/metrics rather than changing the user's display settings. Directory outages and malformed/slow networks are reproduced locally. The exact earlier France Culture incident could not be reproduced because its original saved URL was unavailable; the same hang/cancellation failure class is covered.

No software can guarantee zero defects on every PC. Persistent station, driver or network failures can still prevent playback, but tested failure paths remain cancellable and do not discard saved data.

## Cleanup and preservation

Prior validation/TODO records and 35 station/settings/backup files from obsolete workspace trees were copied to `backups/workspace-before-1.5.2`, with per-file hash verification. They include conservative copies of test profiles. The real `%LOCALAPPDATA%/SmallRadio` profile is retained. The final cleanup keeps current source/tests/tools, vendored dependencies/licenses, the current tested build and release, and these data backups.

Old build/release directories, obsolete preview assets, rejected prototypes and temporary diagnostics were removed. The previous 1.5.1 release was removed only after confirming no process used it. Recursive targets were checked for workspace containment and reparse points. The current `test-data/native-ui-152` profile is retained because it contains station changes made during use. Only synthetic loopback test profiles and the isolated package-check extraction were removed from the current build's temporary data.

## Final workspace cleanup on 2026-10-10

Removed 448 files across 37 checked targets, reclaiming approximately 23.12 MiB. This includes the obsolete 1.5.2 build/release, unused vendor examples and language bindings, 32-bit DLLs/import libraries, SDK help files and duplicate download archives. Retained the latest 1.5.3 build/release, source, tests, build/icon tools, all eight required dependency/license files, and saved-data backups. Required dependency hashes were verified unchanged. The current workspace station profile was preserved because it contains user changes; the real LOCALAPPDATA profile was untouched. The old build's validation logs and package-check evidence were copied to `backups/validation-1.5.2` before deletion.

Deletion targets were resolved within the workspace, checked for reparse points and checked against running application paths. `backups/validation-1.5.3/cleanup-report.json` records the exact removed targets. The existing 542-check result remains valid for the unchanged application source; the existing project built successfully after dependency pruning without compiler warnings. Build evidence: `backups/validation-1.5.3/cleanup-build.log`.

