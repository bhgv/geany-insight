# Lua I²C device

Add a rule in **Port Signals**. The short form uses `scl sda` and
address `0x50`. The full form can specify ports, a fallback address and
an active-low reset. Devices with distinct addresses can share SCL and SDA.
Relative paths resolve from the design source directory.

```text
i2c-lua "i2c-registers.lua"
i2c-lua scl sda 0x52 "i2c-address.lua" reset_n
```

Each instance has its own Lua state and OS thread. All callbacks are optional.

| Callback | Behavior |
| --- | --- |
| `reset()` | Runs at startup and on reset. Reset clears the transmit queue. |
| `address(header)` | Receives the raw 8-bit address byte including R/W in bit 0. Return true to accept and ACK or false to release SDA. Without the callback, the configured address is used. |
| `write(byte)` | Runs for each data byte of an accepted write. |
| `receive(bytes)` | Runs at STOP or repeated START after an accepted write. The 1-based table contains the data bytes. |
| `read()` | Runs when the master asks for a byte and the queue is empty. |

`write`, `receive` and `read` can return nil, one byte, or several.
Nil adds nothing to the queue. Integers from 0 to 255 are sent to the
master on reads in order. While bytes are queued, reading calls neither
`read` nor `receive`. If the queue is empty and `read` returns nil, the
device releases SDA during data clocks. Pull-ups make the master read FF.
The queue and frame each hold up to 4096 bytes.

## Run the complete example

Copy [i2c-demo.sv](../../examples/lua-bus-devices/i2c-demo.sv) and
[i2c-frame.lua](../../examples/lua-bus-devices/i2c-frame.lua) into one directory. Open the
SystemVerilog file, choose **Debug Current Verilog File**, and enter this
recipe in **Port Signals**:

```text
module i2c_demo
duration 1000
i2c-lua i2c-frame.lua
```

Start debugging and choose **Continue** if the simulator stops at startup.
The design checks the reply `10 35` and prints it in the console.

## Examples

- [Address selection, a write without a reply, and a read reply](../../examples/lua-bus-devices/i2c-address.lua)
- [Queued frame reply after repeated START](../../examples/lua-bus-devices/i2c-frame.lua)
- [Registers with a persistent cursor](../../examples/lua-bus-devices/i2c-registers.lua)
- [Two bytes queued by write](../../examples/lua-bus-devices/i2c-write-reply.lua)
