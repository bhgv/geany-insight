-- Load through Tools → Insight: Lua commands → Load extension…
assert(INSIGHT_API_VERSION == "0.5")

commands.register("selection.uppercase", "Uppercase selection", function()
    editor.replace_selection(string.upper(editor.selection()))
end)

commands.register("selection.wrap", "Wrap selection in parentheses", function()
    editor.replace_selection("(" .. editor.selection() .. ")")
end)

local calls = 0
commands.register("selection.number", "Insert next number", function()
    calls = calls + 1
    editor.replace_selection(tostring(calls))
end)
