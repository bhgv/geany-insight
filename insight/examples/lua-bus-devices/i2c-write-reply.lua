-- Write 0x10 and then read two bytes. Other writes queue no response.
function address(header)
    return (header >> 1) == 0x50
end

function write(byte)
    if byte == 0x10 then
        return 0x12, 0x34
    end
    return nil
end

function receive(bytes)
    -- The frame notification can do bookkeeping. The bytes are already queued.
    print(("Received %d I2C bytes"):format(#bytes))
    return nil
end

function read()
    -- The first two reads drain the queue without calling this fallback.
    return nil
end
