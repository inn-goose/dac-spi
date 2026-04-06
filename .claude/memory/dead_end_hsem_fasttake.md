---
name: Dead End - Removing HSEM FastTake
description: HAL_HSEM_FastTake is required — calling Release alone on a free semaphore doesn't trigger interrupts
type: project
---

Tried calling `HAL_HSEM_Release()` without `HAL_HSEM_FastTake()` first. System hangs — no interrupt fires.

**Root cause:** The notification fires on the Take→Release transition. If the semaphore is already free, Release is a no-op (no state change).

**Why:** Documented in CLAUDE.md as "NOT VALID". FastTake is mandatory for interrupt generation.

**How to apply:** Never remove FastTake from hsem_trigger(). The take+release pair is the minimum for notifications.
