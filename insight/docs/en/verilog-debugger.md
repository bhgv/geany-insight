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
This is a functional asynchronous model with uninitialized contents and
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
