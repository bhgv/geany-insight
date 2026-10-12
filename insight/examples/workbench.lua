-- Load through the integrated Scripts > Load extension menu.
commands.register("workbench.file_symbols", "Show current file symbols", function()
    local path = editor.filename()
    if not path then editor.message("Save the file first") return end
    local symbols, total = project.symbols("", false, {path=path, limit=12})
    local names = {}
    for _, symbol in ipairs(symbols) do names[#names+1] = symbol.name end
    editor.message(total .. " symbols: " .. table.concat(names, ", "))
end)
commands.register("workbench.next_use", "Select next use of selected identifier", function()
    local name = editor.selection()
    if name == "" then editor.message("Select one identifier first") return end
    local matches = editor.occurrences(name, 10000)
    local position = editor.position()
    for _, match in ipairs(matches) do
        if match.start > position then editor.set_selection(match.start, match.finish) return end
    end
    if #matches > 0 then editor.set_selection(matches[1].start, matches[1].finish) end
end)
events.on("document_save", function(event)
    editor.message("Saved: " .. event.path)
end)
