-- End the write with CS# high. In the next selection, clock out the reply.
local frames = 0

function reset()
    frames = 0
end

function finish(bytes)
    if #bytes ~= 3 or bytes[1] ~= 0x40 then
        return nil -- Also ignores the later selection containing dummy bytes.
    end
    frames = (frames + 1) & 0xff
    return (bytes[2] + bytes[3]) & 0xff, frames
end
