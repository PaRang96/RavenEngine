# Engine fonts

The engine's default UI fonts, from Google's Noto family, under the SIL Open Font License 1.1 (each font's `*-OFL.txt`, staged beside the application). Downloaded on 2026-11-25 with the owner's approval from `https://github.com/google/fonts` at commit `83c62e30d87daa70216dab6b984d00d20d96d585`. They are unmodified and only renamed: the OFL allows any file name, and the font names inside are unchanged.

| File | Upstream file | Bytes | SHA-256 | Covers |
| --- | --- | --- | --- | --- |
| `NotoSans.ttf` | `ofl/notosans/NotoSans[wdth,wght].ttf` | 2,049,096 | `bfb7bb691513f12e734dc346c03a03f784912432d7e3fa8e56efcf906fe86b3d` | Latin, Greek, Cyrillic |
| `NotoSansArabic.ttf` | `ofl/notosansarabic/NotoSansArabic[wdth,wght].ttf` | 844,676 | `63111b5b2e074dd48cc67692e0a2726d86ee94c1c37fe8598257b7b4e87e869e` | Arabic (right-to-left) |
| `NotoSansKR.ttf` | `ofl/notosanskr/NotoSansKR[wght].ttf` | 10,414,588 | `194018e6b2b293a7964f037b25c0249ce1418bc9ab3c971060a03aa57861e252` | Korean, plus common Han |

They are variable fonts, and the text stack uses each one's default instance. Font fallback tries them in table order. Games add their own fonts as content.
