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
