# SBUS_COUNT=4 vs 8 run discrepancy

## Observed behavior
- `make yuquan_corvus_sim MBUS_COUNT=1 SBUS_COUNT=4` previously tripped a Difftest pc mismatch (pc=0x80000008 vs spike=0x80000004) around 18 cycles into `microbench`.
- `make yuquan_corvus_sim MBUS_COUNT=1 SBUS_COUNT=8` completes the same flow without that pc Diff (though the user reports functional incorrectness that still needs debugging).
- Both runs regenerate Corvusitor outputs under `test/YuQuan/build` before compiling and running the C-model simulator.

## Artifact deltas captured
- Connection graphs are identical: `test/YuQuan/build/YuQuan_corvus_sbus4.json` and `test/YuQuan/build/YuQuan_corvus_sbus8.json` have the same partitions and edges (counts and sets matched by script), so the topology extraction step does not diverge with SBUS_COUNT.
- The generated counts differ at compile time:
  - `test/YuQuan/build/sbus_snapshots/CYuQuanTopModuleGen_sbus4.h:24` sets `kCorvusGenSBusCount = 4`.
  - `test/YuQuan/build/sbus_snapshots/CYuQuanTopModuleGen_sbus8.h:24` sets `kCorvusGenSBusCount = 8`.
- The count constant is pulled into the runtime wrappers and C-model (`test/YuQuan/build/sbus_snapshots/CYuQuanCModelGen_sbus4.h` / `_sbus8.h`), so the number of instantiated SBus endpoints and how remote slots are addressed differ between the two builds even though the discovered netlist is the same.

## How SBUS_COUNT is applied
- Slot assignment uses a round-robin over the available SBus endpoints (`SlotAddressSpace::assign_slots` at `src/corvus_generator.cpp:140-173` and its use for `remote_recv` at ~988). For each remote connection, `bus_index = bus_cursor_`, then `bus_cursor_ = (bus_cursor_ + 1) % sbus_count`.
- With the same ordered remote slot list, SBUS_COUNT=4 packs slots onto endpoints 0-3 (higher collision/queuing), while SBUS_COUNT=8 spreads them across 0-7. The generated workers encode this mapping directly (e.g., `sBusEndpoints[6]->send(...)` in `test/YuQuan/build/CYuQuanSimWorkerGenP0.cpp` for SBUS_COUNT=8; the SBUS_COUNT=4 build would wrap that send to `sBusEndpoints[2]` instead).
- Because bus indices change while the logical connections stay the same, altering SBUS_COUNT changes ordering/backpressure characteristics without changing topology, which explains divergent architectural traces.


