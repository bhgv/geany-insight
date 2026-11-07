-- Match the raw header for 0x52, in either direction. Reject other addresses.
function address(header)
    local target = header >> 1
    return target == 0x52
end

function receive(bytes)
    -- Accept writes without generating a reply. A later read uses read().
    return nil
end

function read()
    return 0x47, 0x59
end
