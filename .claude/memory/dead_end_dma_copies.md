---
name: Dead End - DMA for M4 Memory Copies
description: Full DMA1 burst-mode setup was implemented for M4→M7 copies, worked correctly, but became unnecessary
type: project
---

Implemented DMA1 with INC16 burst mode, FIFO enabled, very high priority for M4 memory copies. Worked correctly.

**Why removed:** The copy-and-verify test harness (M7 writes, M4 copies, M7 compares) was replaced by direct-read playback (M4 reads directly from shared memory via volatile pointer). No copy needed = no DMA needed.

**How to apply:** Optimize the right thing. The best optimization for a copy is eliminating it entirely.
