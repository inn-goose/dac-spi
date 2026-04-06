---
name: Architecture Challenge - Is Dual-Core Necessary?
description: M7 does trivial parsing, M4 does trivial SPI output — single core might handle both, avoiding all IPC complexity
type: project
---

**Challenge:** The dual-core split adds massive complexity — shared memory, HSEM, cache coherency, manual M4 boot, volatile-aware copies. Is it worth it?

**M7 workload:** Parse serial bytes (switch/case + memory write per byte). At 176KB/s (44.1kHz stereo), this is trivial for a 480MHz core.

**M4 workload:** ISR fires at sample rate, does two SPI transactions (~5-10μs per pair). At 44.1kHz, the ISR consumes ~44% of a 240MHz core. On the 480MHz M7, it would be ~22%.

**Single-core estimate:** Serial parsing (~5%) + ISR (~22%) = ~27% M7 utilization. Plenty of headroom.

**The real argument for dual-core: jitter.** On a single core, if serial parsing takes variable time (USB burst, ring buffer drain), the Ticker ISR could be delayed. Cortex-M7 ISRs have priority preemption, so the Ticker would preempt `loop()` — but only if the ISR priority is configured correctly. `mbed::Ticker` uses a high-priority timer interrupt, so it WOULD preempt serial reads. The jitter risk is actually low.

**Counter-argument that keeps dual-core valid:**
1. Future expansion — effects processing, mixing, visualization on M7 while M4 maintains real-time output
2. The M4 is "free" — it's there anyway, running idle if unused
3. The IPC complexity is already paid and working
4. Separation of concerns makes the code easier to reason about (once the IPC is understood)

**Verdict:** Dual-core is defensible but not strictly necessary for the current use case. It's an architectural investment for future capability. Worth noting in blog posts as a deliberate choice, not a requirement.
