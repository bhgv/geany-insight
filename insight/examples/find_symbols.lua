-- Prefix search returns up to 256 entries and the full number of matches.
local matches, total = project.symbols("insight_")
assert(total >= #matches)
-- Exact lookup:
local definitions, count = project.symbols("insight_run", true)
if count > 0 then
    local symbol = definitions[1]
    assert(symbol.path ~= "" and symbol.line >= 1)
end
