# Bundled fonts

Noto Sans (regular/italic variable TTFs) and Noto Sans Mono (variable TTF) are
stored as complete, uncompressed .ttf files with their SIL Open Font License 1.1 texts. They are loaded privately
for this editor process; no system font installation or user font configuration
is changed. On Linux the Fontconfig/PangoFc family cache is refreshed. Windows
uses private AddFontResourceExW registration; Windows GUI verification is deferred.

`manifest.json` records download URLs and SHA-256 hashes. Source is the official
Google Fonts repository: https://github.com/google/fonts/tree/main/ofl/notosans
and https://github.com/google/fonts/tree/main/ofl/notosansmono .

All three files include the Russian alphabet, Ё/ё and №. Verify with:

```sh
python3 insight/tests/theme_assets_test.py
```

Verdana is a suitable proportional interface font and has Cyrillic. You can use
an installed Verdana in panel CSS or Font & Color. Source-theme CSS stacks try
original installed Windows font names, then installed Verdana for UI fonts, then
Noto Sans; code stacks prefer installed Consolas then Noto Sans Mono.

Segoe UI, Consolas, Verdana, Arial, Microsoft Sans Serif and Trebuchet MS files
are not included. A free personal-use download does not grant permission to
redistribute those fonts with an application. Reference:
https://learn.microsoft.com/en-us/typography/fonts/font-faq
and https://learn.microsoft.com/en-us/typography/font-list/verdana .


The three original TTF files total 6,080,144 bytes (about 6.1 MB). No glyphs,
scripts, variation axes or metadata are removed. The loader also accepts optional
gzip-compressed fonts: it detects gzip by its 1F 8B signature, independently of
the filename extension, and uses a private temporary TTF for that process. Raw
fonts are used directly. No compressed fonts are included in this distribution.
