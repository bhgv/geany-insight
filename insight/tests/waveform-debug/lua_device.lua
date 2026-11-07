local writes = 0
local shifts = 0
local reads = 0

function reset()
    writes = 0
    shifts = 0
    reads = 0
end

function shift(bit, index)
    assert(bit == 0 or bit == 1)
    assert(index == shifts % 8)
    shifts = shifts + 1
end

function receive(value)
    if type(value) == "table" then
        assert(#value == writes)
        return value[1], writes
    end
    if value == 0x9f then
        return 0x12, nil, 0x34
    end
end

function write(byte)
    assert(byte >= 0 and byte <= 255)
    writes = writes + 1
end

function read()
    reads = reads + 1
    if reads == 1 then
        return 0x56
    end
    return nil
end


function address(header)
    return (header >> 1) == 0x51
end
