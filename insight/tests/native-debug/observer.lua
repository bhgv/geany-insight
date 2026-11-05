return function(event)
    if event.kind == "native-point" then
        watch("Observed function", event.name)
        log("Lua breakpoint: " .. event.name)
    end
    return true
end
