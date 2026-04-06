---
name: Dead End - Linker Section Placement
description: .RAM_D2 section gives different addresses on M7 vs M4 — each core has its own linker layout
type: project
---

Placed shared buffer in `.RAM_D2` section. M7 saw it at 0x240014A0, M4 at 0x10001728. Each core has its own linker script. M4 read garbage.

**How to apply:** Never use linker-placed variables for shared memory on dual-core STM32. Always use hardcoded addresses in D4 SRAM (0x38000000).
