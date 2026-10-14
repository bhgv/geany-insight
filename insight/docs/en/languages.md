# Language profiles and file bindings

Open **Options → Language profiles…** to edit profiles. Editor right click → **File language / profile…** opens the same dialog with forced binding selected for the current file.

Choose a profile and edit its language name, base Geany parser/lexer, own extensions and compatible extensions. To create a custom profile, select a suitable starting profile and enter a new name. The base parser controls syntax and definition extraction: renaming does not create a compiler or parser.

Separate extensions with semicolons: `c;c++;C;cpp;cxx;h;hpp;hxx;h++`. Matching is case-sensitive on all platforms; specify `s` and `S` separately. Compatible extensions add search inputs without changing their parsers. Default C/C++ compatibility includes C/C++ and ASM/S. Configured profiles restrict definitions, links and text searches to their own/compatible extensions and files forced to the same profile. Other files remain project members but are excluded from these searches.

**Force this profile…** overrides extension and shebang for one file. Named-file bindings persist across launches. Unnamed-buffer bindings last for the session and are recorded for their path after saving. **Reset file binding** restores automatic detection. Bindings belong to paths; reassign after externally renaming a file.

## Colors, fonts and words

**Predefined words** overrides the primary keyword set, separated by spaces. Empty retains the base parser's words. This influences highlighting and excludes keywords from Context links; it does not add index definitions.

**Fonts and CSS** selects current Geany styling or a light/dark starting preset. Choosing a preset replaces the dialog's CSS; editor changes happen only on **Apply**. Choose a category, font, size, Bold/Italic and color, then **Add CSS rule**. Appended rules override earlier declarations. `editor` sets the main font; `.keyword` addresses keywords and `.comment` comments. Semantic categories map to named base-lexer styles; `.style-N` addresses a Scintilla style number for any lexer.

```css
editor { font-family: "Monospace"; font-size: 12pt; }
.keyword { font-weight: bold; font-size: 14pt; color: #2456a8; }
.comment { font-style: italic; color: #68756b; }
.string { color: #8e421c; }
```

Supported selectors: `editor`, `.keyword`, `.comment`, `.string`, `.number`, `.operator`, `.type`, `.preprocessor`, `.style-0` … `.style-255`. Supported properties: `font-family`, `font-size`, `font-weight`, `font-style`, `color`, `background-color`. Specify size in `pt`, from 4 to 96. This is a Scintilla CSS subset without nested selectors or web animation. Apply rejects unsupported selectors/properties.

## Settings files

Profiles occupy `languages/<language name>/` under Geany's configuration directory (`config/languages/` in a portable configuration). `language.conf` stores the name, parser, extensions, words, preset and bound paths; `style.css` stores appearance. Renaming the profile renames its folder; unsafe path characters are rejected.

At first launch, absent standard profiles are populated from current Geany extensions, keywords, colors and emphasis, plus the editor's main font. C/C++/ASM receive initial compatible extensions. Existing language.conf/CSS files are retained. User profiles take precedence over seeded defaults; conflicting own extensions between two user profiles require correction.

Edit these files manually if desired. **Reload profiles** rereads them. **Apply** saves the dialog, reloads profiles, updates open documents and requests workspace reindexing. Invalid parsers/CSS exclude a profile from loading. Profile changes do not modify or save source text.
