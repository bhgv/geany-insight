-- SPI mode 0. Send 0x9f, then clock three bytes to read 0x47, 0x59, counter.
local counter = 0

function reset()
    counter = 0
end

function receive(byte)
    if byte ~= 0x9f then
        return nil -- Dummy bytes used to clock out the response add nothing.
    end
    counter = (counter + 1) & 0xff
    return 0x47, 0x59, counter
end
