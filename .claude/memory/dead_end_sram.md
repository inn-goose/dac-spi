---
name: Dead End - D2 SRAM1
description: STM32H747 D2 SRAM1 (0x30000000) doesn't work for shared memory without clock enable + MPU config
type: project
---

D4 SRAM4 (0x38000000, 64KB) works for inter-core shared memory. D2 SRAM1 (0x30000000) failed — needs clock enable + MPU config, not pre-configured by mbed.

**Why:** ArduinoCore-mbed doesn't initialize D2 SRAM clocks or MPU regions. Don't reattempt without hardware debugging setup.

**How to apply:** Always use D4 SRAM (0x38000000–0x3800FFFF) for shared memory.
