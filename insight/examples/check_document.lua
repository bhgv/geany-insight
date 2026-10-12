-- Load through Tools > Insight > Load Lua extension.
commands.register("insight.check_document", "Insight: Check document", function()
    assert(editor.has("occurrences"))
    local matches, total = editor.occurrences("foo", 100)
    editor.message("Lua " .. INSIGHT_API_VERSION .. ": foo occurrences = " .. total)
    if #matches > 0 and editor.has("set_selection") then
        editor.set_selection(matches[1].start, matches[1].finish)
    end
end, "<Primary><Alt>i")
