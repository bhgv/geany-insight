# Lua SPI device

Add a rule in **Port Signals**. Relative paths resolve from the design
source directory. The short form uses `sck cs_n mosi miso` in mode 0.
The full form can specify different ports and an active-low reset.

```text
spi-lua "spi-byte-queue.lua"
spi-lua sck other_cs_n mosi miso "spi-frame.lua" reset_n
```

Each instance has its own Lua state and OS thread. Variables persist
across callbacks. All callbacks are optional.

| Callback | Behavior |
| --- | --- |
| `reset()` | Runs at startup and on reset. Reset clears the transmit queue. The callback can reset script variables. |
| `shift(bit, index)` | Runs on each rising SCK edge. The index is 0–7. Return 0 or 1 to override the next MISO bit, or nil to use the queue. |
| `receive(byte)` | Runs after each full byte, including bytes the master clocks while reading a response. |
| `finish(bytes)` | Runs on CS# release. The table contains complete bytes and starts at index 1. |

`receive` and `finish` can return nil, one byte, or several, such as
`return 0x12, nil, 0x34`. Integers from 0 to 255 are queued in order and
serialized most significant bit first. Unsent bytes survive CS# release.
An empty queue sends FF. The queue and frame each hold up to 4096 bytes.

## Examples

- [Byte command with a three-byte reply](../examples/lua-bus-devices/spi-byte-queue.lua)
- [Reply after a CS# frame](../examples/lua-bus-devices/spi-frame.lua)
- [Bit-by-bit MOSI echo](../examples/lua-bus-devices/spi-shift.lua)
