---
name: Dead End - HSEM Direct Register Access (RLR)
description: Using HSEM->RLR for 1-step take doesn't fire interrupts — RLR is try-take, fails silently if held
type: project
---

Attempted bypassing HAL with `HSEM->RLR[id]` (1-step take) + `HSEM->R[id] = 0` (release). Semaphore notifications stopped firing. System hung.

**Root cause:** RLR is a try-take that can fail silently. If it fails, no lock-to-unlock transition occurs, so no interrupt is generated. `HAL_HSEM_FastTake` uses a 2-step write-then-verify that always locks.

**Why:** HSEM interrupt fires on state transition (locked→unlocked), not on absolute state. No lock = no transition = no interrupt.

**How to apply:** Always use `HAL_HSEM_FastTake()` + `HAL_HSEM_Release()`. Never replace with direct RLR access for interrupt-based signaling.
