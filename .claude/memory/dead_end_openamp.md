---
name: Dead End - OpenAMP / RPC.h
description: Arduino RPC.h conflicts with manual HSEM handlers — can't coexist, must go fully bare-metal IPC
type: project
---

Tried using Arduino's `RPC.h` for inter-core communication. Linker errors: "multiple definition of HSEM2_IRQHandler" and "multiple definition of HAL_HSEM_FreeCallback" — ArduinoCore-mbed's libmbed.a owns these.

**Why:** OpenAMP internally uses HSEM for its mailbox. Can't have both RPC.h and manual HSEM handlers.

**How to apply:** Fully bare-metal IPC. Must boot M4 manually with `HAL_RCCEx_EnableBootCore(RCC_BOOT_C2)`. Must implement all HSEM handlers from scratch.
