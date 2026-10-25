# QuickStation TODO

## 1.6.14 volume slider clicks — 2026-10-23

- [x] Make a click on the native volume channel select the level at the pointer instead of moving by Windows' default page increment. Map to the thumb's actual travel range, account for its half-width inset, and keep native dragging and keyboard control.
- [x] Check channel clicks at 0%, 20%, 80% and 100%, thumb placement, model updates and native dragging at the tested DPI sizes. Build and package after all five suites pass.

## 1.6.13 meter frame — 2026-10-16

- [x] Replace the recessed meter edge with the same Windows push-button theme used by Play and Mute. Keep the meter passive, square and aligned to the button row; use the classic Windows button frame when themes are unavailable.
- [x] Build and verify the 1.6.13 release at the tested DPI scales.

## 1.6.12 audio meter and control-row layout — 2026-10-15

- [x] Add a compact, boxed audio-level meter at the far right of the Now playing control row. Keep the Play and Mute buttons together, and stretch the native volume slider across the remaining width before the meter.
- [x] Feed the meter from real decoder peaks scaled by QuickStation's volume. Clear it on mute, silence, buffering, reconnect and stop; repaint only the meter when the level alone changes. Keep the meter's colors tied to Windows system colors and preserve the native controls around it.
- [x] Verify the layout at tested DPI scales and cover measured audio, mute and stop with a silent local radio fixture. Build and package 1.6.12 after all five test suites pass.

## 1.6.11 transparent app icon — 2026-10-15

- [x] Replace the dark square badge with the approved transparent blue radio and gold lightning antenna. Let the receiver fill the icon canvas, and keep functional button symbols distinct.
- [x] Regenerate the multi-size `.ico`, embed it in the executable, verify small-size rendering and transparency, and release the tested build.
- [x] Remove the superseded `dist/QuickStation-1.6.10` folder after its running executables exit. Completed on 2026-10-16 after checking for running QuickStation processes; the other superseded 1.6.11–1.6.12 build and release folders were removed in the same cleanup.

## Repository cleanup — 2026-10-13

- [x] Remove previous build trees and release folders, preserve their validation logs, and retain only the 1.6.10 build and package. Remove the unused blue logo and duplicate proposal icon while keeping the saved test profile and source snapshots.

## 1.6.10 approved logo — 2026-10-13

- [x] Apply the approved deep-plum, mint and coral radio logo to the executable's multi-size icon, title bar, taskbar and fallback station artwork. Keep the native playback button symbols for their actions.
- [x] Make the approved 512-pixel artwork the icon generator's source and include native 16, 20, 24, 32, 48, 64, 128 and 256-pixel sizes.

## 1.6.9 remove activity dot — 2026-10-13

- [x] Remove the activity circle, blinking timer state, its layout reservation, and the now-unused audio-level sampling and private IPC field. Give the station name its full native text line again. Keep playback state visible in the station list and status bar.
- [x] Allow the invalid-output-device audio test enough time under a loaded build host, within the player’s 15-second startup limit, and report the observed status if it fails.
- [x] Prepare a separate logo proposal in deep plum, mint and coral with a clear radio body and quick antenna, including taskbar-size previews. The user selected it for the 1.6.10 icon.

## 1.6.8 playback visuals — 2026-10-13

- [x] Align the logo and the Play button's visible left border inside the Now playing frame; the native themed button draws its border one physical pixel inside the control edge. Preserve spacing across the playback controls.
- [x] Replace the audio-reactive, changing-size dot with a small fixed native circle close to the station name. Blink it while playback is Playing and hide it otherwise; remove the owner's custom dot drawing and verify its shape, position, and blink transitions.

## 1.6.7 Now playing optical balance — 2026-10-13

- [x] Correct the overshoot from the four-DIP content shift. The marked screenshot has a 22-pixel top gap and an 18-pixel bottom gap; move the logo, text and controls up by two screen pixels from the original 1.6.5 positions to make the visible gaps 20 pixels each at that display scale. Preserve the frame height and content spacing, and verify with the native UI suite.

## 1.6.6 Now playing inset balance — 2026-10-13

- [x] Initial four-DIP adjustment of the artwork, text and controls, preserving their internal spacing and the group height. The user reported this overshot; 1.6.7 replaces it with the measured two-screen-pixel correction.

## 1.6.5 Now playing layout — 2026-10-13

- [x] Match the approved preview: make the station logo tall enough to span the station name and song-information lines, with both lines centered beside it. Move the logo and text together lower in the Now playing group, keeping a comfortable gap above the Play/Mute/volume controls and balanced space above and below the content. Keep standard Win32 controls, preserve the existing horizontal control spacing, and check the result at common DPI scales.
- [x] Add a small audio-reactive dot immediately after the visible station title, on the same line, as in the approved preview. Let it pulse smoothly with actual playback level (including volume and mute), dim during silence, and stop when playback stops. Reserve room for the dot by shortening long titles with an ellipsis; keep the dot next to the visible title without moving the song line or controls. Limit custom drawing to this indicator, use Windows system colors for themes and high contrast, and verify DPI layout. Physical high-contrast testing remains under Additional environment validation.

## 1.6.4 Now playing alignment — 2026-10-12

- [x] Lower the native station-name and song-title pair by three DIP inside Now playing so its visible text balances against the artwork. Keep the text gap, logo, playback buttons and volume slider positions unchanged.

## 1.6.3 playback spacing — 2026-10-12

- [x] Use the same eight-DIP gap between the two playback buttons and between Mute/Unmute and the volume slider. Preserve the left-aligned native control group and verify it across tested display scales.

## 1.6.2 playback controls — 2026-10-12

- [x] Group Play/Stop, Mute/Unmute and volume left to right under Now playing, aligned with the artwork/list left edge, as in the user-supplied screenshot. Preserve native controls and vertical slider centering.
- [x] Increase the button icon-to-text gap by four 96-DPI pixels, from five to nine DIP, and let the compact button widths grow accordingly. Check the native rendering at multiple DPI scales.

## 1.6.1 Radio Browser outage — 2026-10-11

- [x] Reproduce the reported Bloomberg browse request using QuickStation's WinHTTP code. The live API currently returns 16 stations; the screenshot's HTTP 503 was a service-side failure, not a station-name parsing error.
- [x] Keep one bounded successful search for seven days and use it only for the exact same query if a later request fails. Show when results are cached, and keep malformed/expired cache data from affecting browsing. Cover outage, restart, query isolation, expiry and corruption with local HTTP tests.
- [ ] An initial search cannot return live directory entries while Radio Browser itself is unavailable. On this date, `all.api.radio-browser.info` and `de1.api.radio-browser.info` resolve to the same server, and `/json/servers` publishes only that one mirror. Revisit mirror discovery if Radio Browser publishes independent servers; keep manual URL entry and saved playback independent of the directory.

## 1.6.0 follow-ups — 2026-10-11

Keep standard Win32 controls, bounded background work, readable code and automatic recovery. Checked items have implementation and regression coverage; the remaining investigations need broader codec fixtures or evidence of an expired directory URL.

- [x] **Center native tab labels.** Check Favorites and All stations text horizontally and vertically at common DPIs and with both tabs selected. Correct the native tab sizing/padding without owner drawing or custom styling.
- [x] **Fix the reproduced AAC startup failure.** The user-supplied MONEY_893AAC stream returned HTTP 200 with `audio/aacp` data (64 KiB in 1.9 seconds), while the original BASS DLL stalled past QuickStation's 15-second startup deadline. An Un4seen maintenance build linked by the vendor for Windows Media Foundation's excessive AAC prebuffer started it in about 3.5 seconds and stayed in Playing for 30 seconds. It adds 2,360 bytes, uses the existing BASS license, and needs no extra AAC plugin. The test probe is optional and does not log or package the station URL.
- [ ] **Broader AAC/HLS compatibility matrix.** Check additional redistributable direct AAC and HLS variants, including behavior on separate Windows builds, when suitable fixtures are available. The existing stalled-HLS fixture and plugin-failure tests cover failure handling but cannot establish support for every codec or server. Re-evaluate the maintenance build when Un4seen issues a numbered release.
- [x] **High priority — improve persistent format-error recovery.** Distinguish repeated unsupported-format/codec failures from ordinary network interruptions and temporary non-audio responses. Bound rapid retries, then use a longer automatic cooldown/probe with a clear status for persistent failures. Keep Stop and station switching responsive and allow a fresh Play attempt to retry immediately. Preserve automatic network reconnect and recovery when a server becomes healthy; avoid requiring a manual engine restart. Test both persistent failures and recovery after several temporary failures.
- [x] **Check and report HLS plugin initialization.** Capture a failed BASS_PluginLoad result and its exact error before subsequent BASS calls overwrite it. Provide useful bounded diagnostics that distinguish plugin/dependency failures from stream/codec failures, preserving the existing private-log policy. Keep ordinary non-HLS playback available when the HLS plugin is unavailable. Include missing/invalid plugin checks and a regression check of audio-initialization error propagation; that initialization reporting already exists and must not be treated as a missing feature.
- [x] **Fix reordering in Favorites and filtered All stations.** Move a station relative to the preceding/following visible station rather than the adjacent entry in the complete library. Preserve selection, filter, favorites and saved order. Disable moves at the visible boundaries and cover interleaved hidden stations, filtered matches and persistence in regression checks.
- [x] **Make context-menu targeting explicit.** Use the right-click position to identify and select the clicked station without starting playback. Suppress station actions over empty space. Preserve keyboard context menus for the selected/focused row and place them beside that row. Verify that favorite/edit/reorder/delete commands operate on the intended station; wrong-row behavior remains a risk to reproduce, not a previously demonstrated runtime bug.
- [x] **Retry exhausted logo downloads after a cooldown.** Allow a logo that failed all three initial attempts to be scheduled again after a longer delay, so server recovery does not require restarting the app. Keep deduplication, bounded tracking, cancellation and search priority intact; prevent request-table exhaustion during station additions/removals. Test exhaustion, later recovery and duplicate requests.
- [x] **Bound and maintain the logo cache.** Add simple background cleanup for unused/old cache entries and a sensible byte/file-count budget. Protect current/in-flight logos, handle file-access errors gracefully, and keep disk scans off the UI thread. Verify that removal does not affect station data and that an evicted logo can be downloaded again.
- [ ] **Investigate expiring directory stream URLs.** Establish whether affected stations save temporary resolved URLs. If confirmed, consider retaining the Radio Browser UUID and original URL as optional, backward-compatible fields, with bounded background refresh after relevant failures. Preserve manually entered/edited URLs and direct playback of saved stations when Radio Browser is offline. Do not assume URL refresh will repair every unavailable station or add a mandatory directory dependency.
  Current finding: Radio Browser defines `url_resolved` as the result of following playlists and redirects, which may change later. No saved station in this workspace has yet shown a proven expired resolved URL. Keep direct offline playback and the current storage schema until a reproducible case identifies the correct fallback.
- [x] **Measure large-library filtering before optimizing.** Benchmark typing and list rebuilding with up to the supported 5,000 stations. Add a short input debounce or another small change only if measurements show noticeable UI delays. Preserve immediate favorites actions, accurate empty-list guidance, selection and accessibility; avoid a virtual-list rewrite without evidence that it is needed.
- [ ] **Validate full-library filtering during sustained playback on slower hardware.** The UI benchmark ran with the audio worker stopped. Playback is in a separate process, but concurrent latency on a low-end Windows PC remains an environment test, not a proved result.

Release-gate evidence: **596 checks passed across five suites** in `backups/validation-1.6-history/1.6.0-vendor-release-check.log`. The 5,000-station input benchmark measured 0 ms for the keystroke and 409 ms for the delayed rebuild on this host; concurrent live playback performance was not separately measured. See VALIDATION.md for the AAC probe and remaining environment limits.

## Completed in 1.5.2

Follow-up in **1.5.3**: slightly increase the playback-button icon/text gap to five DIP. Final cleanup removed the previous build/release, unused vendor examples and bindings, 32-bit binaries, and duplicate download archives. The latest build/release, required x64 dependencies, saved profile and backups remain.

- [x] Restore fully default Windows menu drawing. Keep the native Stations / Options / Help menu, original spacing, hover, keyboard behavior and system colors. Remove all menu owner drawing, custom measurement/background code and separator workarounds.
- [x] Center playback-button icons and text together with consistent four-DIP spacing and balanced outer padding. Use compact, stable widths measured separately for Play/Stop and Mute/Unmute. Keep native buttons and image lists. The 1.5.3 follow-up increases this gap to five DIP.
- [x] Give both status-bar messages consistent native insets, retaining the system height, separator, sizing grip and overflow tooltip.
- [x] Slightly reduce Favorites and All stations row height: 32-DIP logos with three DIP above/below, approximately 38-DIP image cells. Retain native full-row selection and existing horizontal spacing; keep Browse text rows unchanged.
- [x] Remove the permanent All stations instruction line. Keep the local filter, move instructions into Help → Using QuickStation (F1), and show guidance only when a list is empty. Correctly relayout when filters change between empty and nonempty results.
- [x] Audit reliability and code quality, fix confirmed gaps, and add regression coverage. Bound and cancel actual directory HTTP requests; preserve files when encoded libraries exceed the load limit; report missing audio dependencies without blocking loader dialogs; bound on-disk logo decoding. Review recovery, ownership, generations, shutdown and data preservation. Consolidate shared helpers and remove dead menu code.
- [x] Clean obsolete builds/releases, rejected prototypes, unused preview assets and temporary diagnostics. Preserve station/settings backups and the current workspace profile; retain the verified 1.5.3 build/release, dependencies and licenses. Earlier validation evidence is archived separately.

Verification: **542 checks passed across five suites**, with no compiler warnings. The extracted release was inspected on Windows 11, including live playback/metadata and minimize/restore. Details and remaining environment limits are in [VALIDATION.md](VALIDATION.md).

## Additional environment validation

These cases remain unverified because the required separate environments/hardware were not available: clean Windows 10/11 machines without developer tools; actual high contrast and physical mixed-DPI monitors; unplugged/no-device audio; broader codec variants; suspend/resume and multi-day listening. Automated checks cover reproducible network, helper, persistence and UI failures, but do not establish zero defects on every PC.

## Earlier work

Earlier completed tasks and superseded design choices are preserved in [the previous TODO record](backups/workspace-before-1.5.2/TODO-previous.md). The current rules are standard Win32 controls, default Windows drawing, no added menu/header separators, and no gridlines or striped rows. Now playing remains below the permanent station list. Existing playback recovery, local storage and window-placement persistence remain enabled.
