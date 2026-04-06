---
name: Dead End - memcpy with volatile
description: memcpy() doesn't respect volatile semantics — use explicit volatile-aware copy functions with 32-bit ops
type: project
---

Used `memcpy()` to copy between volatile shared memory regions. Data corruption — stale/wrong values.

**Root cause:** C standard doesn't require memcpy to respect volatile. Compiler may use wide loads, reorder, or cache in registers.

**How to apply:** Use the explicit `uint8_to_int16()` / `int16_to_uint8()` functions in core_mem.h. They use `volatile uint32_t*` reads/writes that are guaranteed to hit memory.
