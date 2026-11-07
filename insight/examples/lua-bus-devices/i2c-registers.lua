-- Write a register index followed by any number of values.
-- Repeated START plus read reads from the current index and advances it.
local registers, cursor, first_write

function reset()
    registers, cursor, first_write = {}, 0, true
end

function address(header)
    if (header >> 1) ~= 0x50 then
        return false
    end
    first_write = true
    return true
end

function write(byte)
    if first_write then
        cursor, first_write = byte, false
    else
        registers[cursor] = byte
        cursor = (cursor + 1) & 0xff
    end
    return nil
end

function read()
    local value = registers[cursor] or 0
    cursor = (cursor + 1) & 0xff
    return value
end
