---
name: Dead End - Memory Barrier Evolution
description: __ISB() removed (not needed for data ops), __DSB() downgraded to __DMB() — both changes worked
type: project
---

Cache operations originally used `__DSB(); __ISB();`. Refined in two steps:

1. **Removed __ISB()** — flushes instruction pipeline, only needed when executing code from cleaned memory. Not needed for data operations. Worked.

2. **Downgraded __DSB() to __DMB()** — DMB ensures ordering without full pipeline stall. Sufficient for shared memory between cores. Worked.

**How to apply:** Use `__DMB()` after cache clean/invalidate for data-only shared memory operations. Use `__DSB()` only if you need to guarantee completion before next instruction. Use `__ISB()` only for self-modifying code scenarios.
