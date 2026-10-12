-- Select text, then run this script from Geany's Tools menu.
-- string.upper affects ASCII; Unicode case folding needs a future host API.
editor.replace_selection(string.upper(editor.selection()))
