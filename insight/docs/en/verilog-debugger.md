# Verilog and SystemVerilog debugger

Open a saved `.v` or `.sv` testbench in Geany. Choose
**Tools → Verilog / SystemVerilog → Debug Current Verilog File**.
The same command is available in the editor context menu under
**Source tools**. Save any edits before starting.

Use **Debug Verilog Testbench…** to select several files when the design
and its testbench are in separate files. **Run Verilog Testbench…** runs
the selected files without stopping at debugger prompts.

In **Verilog Simulation**, select a source line and add a breakpoint.
Use **Continue**, **Step HDL**, **Step Current Process** and **Next Tick**
to move through execution. **Process Values** displays the selected
process or frame. **Load Lua Observer…** opens an observer script.
Simulator messages and commands appear in the embedded console.

**Preview Waveform** opens the current capture. In the waveform window,
use **Signal Map → HDL Uses… → Connections** to inspect data, guard and
clock dependencies. **VCD Waveforms…** opens an existing recording.

## Port signals

**Debug Current Verilog File** opens **Port Signals**. Leave the recipe
empty for an existing testbench. For a module with ports, enter its name
and one rule per port. For example:

```text
module counter
duration 1000
clock clk 5 5 5
pulse rst_n 0 20 0
bus data 8 40 "step * 3"
output count 8
```

Times are in nanoseconds. `clock` takes the start, high and low durations.
Use `clock-hz clk 5 100000000` for a 100 MHz clock with equal phases.
`pulse` takes start, length and active level. `value data 8 "8'h2a"`
drives a constant. `bus` takes the width, update interval and expression.
`step` starts at zero, and SystemVerilog expressions can also use
`$time`. Use double quotes around expressions with spaces or Verilog
literals containing an apostrophe. `output` records a port in the VCD.

Port recipes are saved by project and source file. **Port Signals…**
on the debugger panel reopens the recipe. Edits made during a run apply
the next time debugging starts.

### Graphical device setup

In **Port Signals**, click **Add Test Device…** and select the rule type.
Use **module** to enter the design module name if the recipe is empty.
The form fills in typical port names and values. Change the fields to match
the design. Memory devices offer an optional image picker. Lua devices
require a script and accept an optional active-low reset signal.

Click **Insert Rule** to append the validated rule. A memory image is
inserted immediately before its device. Repeat for each device, then start
debugging or save for the next run. The recipe remains editable. Expand
**Text rule reference** to see the full syntax. The full recipe is checked
when you save or start, including conflicts with existing ports.

Use the **Ports** and **Source** tabs to read and copy interface names
while setup is modal. **Ports** lists names, directions and every rule
that references each port. **Unassigned** indicates no binding. Each
rule includes its line number. Select a row and click **Copy Port Name**.
The **Source** tab allows text selection and copying. The list updates as rules change.
Icarus expands macros and includes with the same search path used for
simulation. Only active `ifdef`, `ifndef`, `elsif`, and `else` branches
contribute ports. If preprocessing fails, **Ports** shows the diagnostic
and **Source** still shows the original text.

The device selector shows an icon and title next to each rule name.
**Find device** searches titles and rule names without regard to case.
**Device Regex** enables regular expressions. For example, **^spi-** in
**Memory** finds SPI memories. A device can belong to multiple categories.
SPI EEPROM appears in **SPI** and **Memory**. **Constant signals** offers
**value**. **Hex tables** adds **hex-table PORT [WIDTH=32]**, which shows up
to eight bytes, most significant first, with byte offsets and hex values.
Entirely unknown nibbles show X or Z. Mixed nibbles show ?.

Each port field shows its required width. Focusing it highlights compatible
directions in the table. **Bits** shows each port's width. Double-click a
compatible port to replace the field's contents. A wider port uses the
low-order slice in declaration order, for example `data[7:0]` for
`[15:0]` or `data[8:15]` for `[0:15]`. Ports that are too narrow or
whose width is unknown are unavailable for automatic assignment.
A width of `?` indicates a range that needs parameter or type evaluation.

To choose other bits, select the port and click **Choose Slice…**.
Set the offset from the least significant end and check the Verilog
preview. Offset 0 gives the same slice as double-clicking.

**Find ports** matches part of a port name without regard to case.
In the wizard, **Compatible only** keeps ports with the required direction
and enough known bits for the selected field. Changing the field width
updates the list. The two filters can be combined.

Enable **Regex** to match port names with a regular expression.
For example, **^data_[0-9]+$** matches names such as **data_12**.
Matching is case-insensitive. An invalid pattern shows an error icon
with a diagnostic tooltip. **Sort by** orders ports by name, direction,
occupancy, or declaration order. Occupancy puts unassigned ports first.
**Reverse** reverses the selected order.

Next to **Find ports**, choose whether to search direction, devices and rules,
or all searchable columns. Both literal and regex matching use the selected
scope. **Unassigned only** keeps free ports and **Assigned only** keeps
ports with bindings. These filters combine with search and compatibility.

While **Port Signals** remains open, the wizard shares its query, search
scope, sorting and filters with the main list. Reopening the wizard also
restores the last selected device type.

In the main **Port Signals** window, select an assigned port and click
**Go to Rule…**, or double-click its row. The recipe editor scrolls to
and selects the rule. For multiple bindings, choose by line number
and rule text. In the wizard, double-click still assigns the port.

The wizard saves a slice as a `port-slice` rule and uses its alias in the
device rule. Both rules form the connection. The table lists the device
against the original port.


**Preview Testbench…** shows the SystemVerilog generated from the current
recipe. **Dependencies** lists model sources and files staged for launch.
Invalid recipes show a diagnostic. The preview is copyable and leaves the
saved recipe and simulator state untouched.

The separate **Preview Diagram…** button opens the connection diagram.
DUT ports and widths appear in the center, stimulus on the left and
devices and probes on the right. Arrows show direction, with a two-headed
arrow for inout. The diagram includes slices and all bindings, even those
hidden by the port table filters. The preview leaves the simulator idle.

## Triggered waveform capture and live controls

In **Port Signals** for the current file, configure one optional
`capture-trigger`. The generated testbench starts VCD capture on the first
match and continues to the configured simulation end. The capture uses
the original simulation timestamps. Without a trigger, capture starts
at time zero.

```text
module my_controller
duration 1000000
button start
switch enable
capture-trigger rise start
```

Use `capture-trigger fall start` to capture when the button pulse ends.
The `button PORT [IDLE [LENGTH_NS]]` rule creates a control in **Live Port I/O**.
Each click generates one pulse, with an idle level of 0 and a length of
100 ns by default. `switch PORT [INITIAL]` holds the level chosen in the
panel and starts at 0 by default.

To match a bus, use a sized Verilog binary or hexadecimal literal
of up to 4096 bits. Put the literal in double quotes in the recipe.
A question mark ignores a bit in binary or a nibble in hexadecimal.

```text
capture-trigger match core.status[127:0] "128'h0123456789abcde?0123456789abcdef"
```

Names are relative to the DUT and may include a fixed bit or range.
Use one bit for edge triggers. The console reports when capture begins.
If the simulation ends first, the debugger reports that the trigger
did not fire.

## Live indicators and slider

Add these rules in **Port Signals** to connect controls and displays in
**Live Port I/O**. Each name is a port of the design.

```text
slider gain
led ready
led busy_n 0
display status
display count 32 dec
```

- `slider PORT [WIDTH [INITIAL]]` drives an unsigned input. It defaults
  to 8 bits and zero. Widths from 1 to 16 bits are supported.
- `led PORT [ACTIVE]` shows a single output as ON or OFF. It defaults
  to active-high. Use 0 for active-low. X and Z have separate readings.
- `display PORT [WIDTH [hex|dec|bin]]` shows an unsigned output from
  1 to 64 bits. It defaults to 8-bit hexadecimal. If a value contains
  X or Z, the display shows the original bits.

The instruments update at the configured `live-interval`. A slider
change takes effect on the next input sample.

## Live plots

Add any of these rules to **Port Signals**, then expand **Live Plots**
inside **Live Port I/O** during debugging.

```text
scope counter[7:0]
scope-xy x_position y_position
scope-xyz core.x core.y core.z
```

The time plot uses simulation nanoseconds. XY plots two signal values
against each other. XYZ adds a third signal using a fixed projection.
Each axis accepts an unsigned signal or slice up to 64 bits.
Names are relative to the design instance and can refer to internal signals
or ports already used by other devices.

All axes in a plot are read together at the configured `live-interval`.
Each plot keeps the latest 512 samples and scales to their known values.
X and Z leave gaps. Up to eight plots can run at once, including before
a VCD capture trigger fires. Transitions between sample times may be
visible in the VCD without appearing in a live plot.

## Output instruments

Open **Signal Instruments…** in the VCD viewer, including from
**Preview Waveform**. Choose up to three recorded signals. One-bit
signals have a lamp. Known values up to 64 bits appear as unsigned
decimal and hexadecimal. Unknown states remain visible as recorded.

![Cursor readings and the XY trace](../media/hdl-instruments.svg)

Use the waveform tracks for plots against time. Choose X and Y for an
XY trace and add Z for a fixed XYZ projection. The plot follows the
visible time interval and uses transitions in those signals. Unknown
samples interrupt the trace. The first 2048 samples are shown when
the interval is dense. Zoom in to inspect more detail. The readings
follow the VCD cursor, and named VCD sessions save the axis selections.

## Live inputs, output readings and SRAM

Add rules such as these to the port recipe:

```text
live-interval 10
live-input enable 1 0
monitor ready 1
monitor result 16
sram addr dq ce_n we_n oe_n 10 8
```

**Live Port I/O** accepts binary input (including `x` and `z`).
Click **Apply Input** while running or paused. The next simulated poll
applies the most recent value. Monitors refresh as samples arrive.
`live-interval` sets the polling interval in nanoseconds. A short wall-clock
delay at each poll leaves time for interaction. Simulation time remains
controlled by the testbench and its duration.

The SRAM rule connects address, bidirectional data and active-low chip,
write and output enables. The last two numbers set address and data widths.
Geany ships this C model as an Icarus VPI module. It is a functional asynchronous model with uninitialized contents and
high-impedance data outside reads. It supports 1–16 address bits and
1–64 data bits. SRAM timing constraints and vendor-specific behavior are
outside this model.

## UART console

```text
duration 100000
live-interval 250
uart rx tx 1000000 console
```

This connects one asynchronous 8N1 UART to the design's RX and TX.
The baud rate is in bits per second. The optional `console` keyword
adds **UART Console…** to the debugger panel. Press Enter to send the
entry's UTF-8 bytes without an automatic newline. The receiver displays
ASCII and escapes other bytes as `\xNN`. The entry waits for the previous
message to finish transmitting before accepting another one.

## External SystemVerilog device models

Use a simulator-compatible model you already have. The source path is
relative to the current HDL file, or absolute. Quote paths with spaces.
Add companion compilation units with `model-source`. The source
directories are also added to Icarus's include search.

```text
model mem external_memory "models/external memory.sv"
model-param mem DATA "8'h3c"
model-value mem mask "8'h05"
monitor ready 1
model-share mem ready enable
model-link mem external_data data 8
```

`model-link` creates a shared wire of the specified width between a DUT
port and a model port. It also works for bidirectional buses.
`model-share` attaches a model port to an already configured DUT signal.
`model-param` passes a named parameter and `model-value` ties a model
port to an expression. Declare the instance before its bindings.

## Memory presets

In **Port Signals**, enter just a device name to use its standard
lowercase port names and default parameters. For a DUT with ports
`sck`, `cs_n`, `mosi` and `miso`, a complete recipe is:

```text
module my_controller
spi-nor
```

| Preset | Default port order | Parameters |
| --- | --- | --- |
| `sram` | `addr dq ce_n we_n oe_n` | 16 address bits, 8 data bits |
| `sdram` | `ck cke cs_n ras_n cas_n we_n a ba dq dqm` | x16, BL1, CAS 3 |
| `ddr2` | `ck cke cs_n ras_n cas_n we_n a ba dq dqs dm` | RL 3, WL 2 CK cycles |
| `ddr3` | `ck reset_n cke cs_n ras_n cas_n we_n a ba dq dqs dm` | RL 3, WL 2 CK cycles |
| `ddr4` | `ck reset_n cke cs_n act_n a ba bg dq dqs dm_n` | RL 3, WL 2 CK cycles |
| `spi-nor`, `spi-nand`, `spi-eeprom`, `spi-psram` | `sck cs_n mosi miso` | SPI mode 0, built-in geometry |
| `i2c-eeprom` | `scl sda` | 7-bit address `0x50` |

To override ports, list them in order. Use `-` in positions that should
keep the preset value, for example `spi-nor - flash_cs_n - -`. SRAM
widths and DDR latencies can be omitted at the end or overridden
individually. For example, `sram - - - - - 12` keeps 8-bit data and uses
12 address bits. With a bare preset, key `memory-image` by `addr` for
SRAM, `a` for DDR and `cs_n` for SPI. With a custom port name, use that
name instead.

## Built-in functional DDR4 x8

```text
ddr4 ck reset_n cke cs_n act_n a ba bg dq dqs dm_n 3 2
```

The C VPI device connects one rank with a 17-bit multiplexed address,
two bank bits, two bank-group bits, eight DQ bits, a single-ended DQS,
and active-low byte mask. The final numbers set read and write latency
in rising CK cycles. The model opens rows, handles fixed BL8 reads and
writes, supports auto-precharge and explicit precharge, and retains
four-state bytes. Storage is sparse with a 65,536-byte limit.

This functional model is intended for controller data-path debugging.
It supports one outstanding burst. The read strobe is edge-aligned
with CK. Training, differential signaling, mode-register configuration,
refresh timing, power states and JEDEC timing checks are outside its scope.

## Built-in functional DDR3 x8

```text
ddr3 ck reset_n cke cs_n ras_n cas_n we_n a ba dq dqs dm 3 2
```

The C device takes separate RAS#, CAS# and WE# signals, a 16-bit address,
a 3-bit bank, eight DQ bits, single-ended DQS and active-high DM.
It shares the functional BL8 behavior, latency units and limits described
for DDR4 above.

## Preload SRAM, DDR3 or DDR4

Put a `memory-image` line before the device rule and name the same DUT
address port. The path can be absolute or relative to the open Verilog
source directory. Quote paths containing spaces.

```text
memory-image sram_addr "fixtures/boot words.hex"
sram sram_addr dq ce_n we_n oe_n 12 16
```

The image is a plain text file of up to 1 MiB. Separate hexadecimal
words by whitespace. An optional `@hex_address` changes the address
for the next word. The default starting address is zero. A token
beginning with `#` starts a comment through the end of the line.

```text
# Two consecutive SRAM words, starting at address 0x10
@10 BEEF 1234
```

SRAM addresses count words of the configured width. DDR images contain
one byte per address, using `(row << 14) | (bank << 10) | column`. For
DDR3, bank is BA (0–7). For DDR4 it is `(BG << 2) | BA` (0–15).
The saved recipe keeps the path. Each new session reads the file again
and applies it before the device responds to its first bus event.

## Built-in functional DDR2 x8

```text
ddr2 ck cke cs_n ras_n cas_n we_n a ba dq dqs dm 3 2
```

This profile uses a 14-bit address and three bank bits. It starts with
closed banks and uses the DDR3 BL8 behavior, DM polarity and image
layout. Use the DUT address port as the `memory-image` key.

## SPI memories

```text
memory-image flash_cs_n "fixtures/flash.hex"
spi-nor sck flash_cs_n mosi miso
```

The built-in C devices use four-wire SPI mode 0, active-low chip select
and MSB-first bytes. MISO floats while the device is deselected.
Use `spi-nand` or `spi-eeprom` with the same port order.
Devices can share SCK, MOSI and MISO with a separate CS# for each.
Each image uses byte addresses and is keyed by that device's CS# port.
Uninitialized bytes start at FF.

| Model | Geometry | Implemented commands (hex) |
| --- | --- | --- |
| SPI NOR | 64 KiB, 256-byte pages, 4 KiB sectors | 03 read, 02 page program, 20 sector erase, 05 status, 06/04 write enable/disable |
| SPI NAND | One block of 64 × 2048-byte pages | 13 page to cache, 03 cache read, 02 program load, 10 program execute, D8 block erase, 0F get feature, FF reset, 06/04 write enable/disable |
| SPI EEPROM | 64 KiB, 128-byte pages | 03 read, 02 page write, 05 status, 06/04 write enable/disable |

NOR takes three address bytes and EEPROM takes two. NAND page commands
take three row bytes. Its cache read takes two column bytes followed by
one dummy byte. For NAND images, byte address = page × 2048 + column.
These profiles model data transfers with immediate completion.
Device identification, busy timing, protection, ECC and spare areas
are outside the implemented command sets.


## I²C EEPROM

```text
module my_controller
memory-image sda "fixtures/config.hex"
i2c-eeprom
```

The preset connects `scl` and `sda` at 7-bit address `0x50`. The image
is optional. Use the SDA port as the image key and byte offsets in the
hex file. Uninitialized bytes read as FF.

Use `i2c-eeprom I2C_SCL I2C_SDA 0x54` for custom port names or a different
address. The generated harness provides pull-ups. The DUT should pull
the lines low or release them. The device implements current-address,
random and sequential reads, and writes to 128-byte pages in 64 KiB
of storage. A page write wraps within the page and commits on STOP.
Completion is immediate. The profile models one EEPROM on each
configured bus pair.


## SPI PSRAM

```text
module my_controller
memory-image cs_n "fixtures/psram.hex"
spi-psram
```

The built-in C model has 8 MiB of byte-addressable memory and uses
single-bit SPI mode 0. The default ports are `sck cs_n mosi miso`.
Use `spi-psram sck ram_cs_n mosi miso` for a dedicated chip select on
a shared bus. Key the optional hex image by the actual CS# port.

The device accepts three address bytes, with the low 23 bits selecting
memory. It supports read (03), fast read (0B, one dummy byte), write
(02), 32-byte wrap toggle (C0), and reset enable/reset (66/99).
Writes directly replace bytes and require no write-enable command.
Bursts are linear by default. Reset restores linear mode and retains
the model's contents. Bytes outside the preload start at FF.
The profile models the SPI data path with immediate completion.
Quad transfers and physical timing are outside its scope.

## Synchronous memory

- [SDR SDRAM x16](verilog-sdram.md) covers commands, byte masking and image preload.

## Lua devices

- [SPI](verilog-lua-spi.md) covers bit shifts, replies to byte commands and CS# frame handling.
- [I²C](verilog-lua-i2c.md) covers address selection, write callbacks and queued replies to reads.

## Navigate to errors

Icarus compilation diagnostics appear in **Messages**. The debugger
opens the first available source location, scrolls to it, and selects it.
Click another diagnostic to open and select its location. When the tool
supplies only a line number, the whole line is selected.
