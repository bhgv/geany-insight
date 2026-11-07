# SDR SDRAM test device

In **Port Signals**, use the `sdram` preset to attach a built-in x16 SDR
SDRAM device. The default port names are `ck cke cs_n ras_n cas_n we_n a ba dq dqm`.
For custom names, list the ports in that order. The address is 13 bits,
BA and DQM are 2 bits, and the bidirectional DQ is 16 bits.

```text
module my_controller
memory-image a "fixtures/sdram.hex"
sdram
```

The optional image uses 16-bit hexadecimal words and word addresses.
Use `(row << 11) | (bank << 9) | column`. For example, `@c04 beef`
loads row 1, bank 2, column 4. Unwritten words read as X. The model
addresses 4 banks × 8192 rows × 512 columns and stores up to 65536 distinct
words per session.

The model starts with BL1 and CAS 3. LOAD MODE REGISTER selects sequential
BL1/2/4/8, CAS 2/3, and optional single-location writes. The device supports
ACTIVATE, READ, WRITE, PRECHARGE, auto-precharge, and BURST TERMINATE.
The first write beat is sampled on the WRITE command edge. DQM masks
write bytes immediately and read output two enabled clock edges later.
The model handles one outstanding burst. Refresh, power modes, and
device timing checks are outside this functional profile.
