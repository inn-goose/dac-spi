---
name: Dead End - Rejected Optimizations
description: User correctly filtered out non-production optimizations — batch activate (runs once), loop unroll (test data), DMA (test-only)
type: project
---

Three optimizations were suggested and explicitly rejected:

1. **hsem_activate_notification_batch()** — User: "it runs once, don't care"
2. **4x loop unrolling for random gen** — User: "it's just test data"
3. **DMA for M4 copies** — User: "that was solely for tests, remove it"

**Why:** User consistently applied the filter: "does this affect the hot path in production?" If no, skip it.

**How to apply:** When suggesting optimizations, consider whether the code path is production-critical or test/init-only. Don't optimize one-time init code or test harness code.
