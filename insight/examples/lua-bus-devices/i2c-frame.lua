-- Write two bytes to 0x50, then issue repeated START and read two bytes.
-- The reply is already queued when the read address arrives.
function address(header)
    return (header >> 1) == 0x50
end

function receive(bytes)
    if #bytes ~= 2 then
        return nil
    end
    return bytes[1], (bytes[1] + bytes[2]) & 0xff
end

function read()
    -- Called only after the two queued bytes have been consumed.
    return nil
end
