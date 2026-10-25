# Third-party components

## BASS 2.4.18 series and BASSHLS 2.4.5

Copyright Un4seen Developments. Source: https://www.un4seen.com/bass.html

These components are proprietary, not open-source. They are free for qualifying non-commercial use. Commercial use requires an appropriate license from Un4seen. The bundled `bass.txt` and `basshls.txt` contain the full terms; those terms govern these components.

Downloaded from the official vendor on 2026-10-07. Archive SHA-256:

- `bass24.zip`: `3A03EC9A33D0F4F9D167660DA51C8BB1432E8977496995455AB137277D69636E`
- `basshls24.zip`: `7531BC714DDE5A996D1C106B6F7AD08103611DE05BD38423D46A01711AF7E574`

The x64 `bass.dll` used since QuickStation 1.6.0 is an Un4seen vendor maintenance build from
`https://www.un4seen.com/stuff/bass.zip`, downloaded on 2026-10-11. The vendor linked this build
in `https://www.un4seen.com/forum/?topic=20857.0` as a fix for delayed AAC stream startup on
current Windows 11 Media Foundation. It is not yet a
numbered public BASS release. Archive SHA-256: `D79B106D16DD66A917440415FEB80A73E0B412EE04D62F67C331210DEEC5EC65`.
Extracted x64 DLL SHA-256: `0860D33673912A1518A9E7767A1C19FA0D2EE5E3341B63FD6E4AD7A3727A6CC0`.
The bundled BASS license text comes from the official BASS package; BASSHLS remains the official
2.4.5 build. The maintenance DLL is 169,232 bytes, 2,360 bytes larger than the prior DLL.

The original download archives and unused SDK examples/bindings were removed during workspace cleanup. The required x64 DLLs, BASS C header/import library, and full license notices remain vendored; the hashes above record the original download provenance.

## JSON for Modern C++ 3.12.0

Copyright Niels Lohmann and contributors. MIT license, included in `json-LICENSE.txt` / `third_party/json-LICENSE.txt`.

Source: https://github.com/nlohmann/json/tree/v3.12.0

Vendored single-header SHA-256: `AAF127C04CB31C406E5B04A63F1AE89369FCCDE6D8FA7CDDA1ED4F32DFC5DE63`

## Directory and starter stations

Station discovery uses the public Radio Browser API: https://docs.radio-browser.info/

Starter stations and logos are provided by SomaFM: https://somafm.com/

Station names, logos and broadcasts belong to their respective owners. The application caches logos locally for display. No broadcaster affiliation is implied.
