-- Echo MOSI with a one-bit delay. The first MISO bit uses the idle byte FF.
local shifted = 0

function reset()
    shifted = 0
end

function shift(bit, index)
    shifted = shifted + 1
    -- index is 0–7. This value takes effect on the next falling SCK edge.
    return bit
end

function finish(bytes)
    -- A partial byte still calls shift, but is absent from the frame table.
    print(("Shifted %d bits; received %d complete bytes"):format(shifted, #bytes))
    shifted = 0
end
