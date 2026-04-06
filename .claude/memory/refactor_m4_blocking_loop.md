---
name: Refactor - M4 Blocking Playback Loop
description: core_m4.h loop_m4() blocks with while(is_busy()) __NOP() — wastes cycles, prevents overlapping region reads
type: project
---

**File:** `core_m4.h:90-93` and `core_m4.h:112-115`

**Problem:** After starting playback, M4 spin-waits:
```cpp
while (dac_output.is_busy()) {
  __NOP();
}
```

This burns CPU cycles doing nothing. The M4 can't do anything useful during playback — it can't prefetch the next region's header, it can't signal readiness early, it can't do any preprocessing.

**Why it matters:** At 44.1kHz stereo with 16368 samples per region, each region plays for ~185ms. M4 is stuck in a NOP loop for that entire time.

**Potential fix:** Use the `get_completed_region()` mechanism that already exists in DacOutput. The ISR sets `_completed_region` when playback finishes. The M4 loop could poll `get_completed_region()` and interleave other work:
```cpp
void loop_m4() {
  int completed = dac_output.get_completed_region();
  if (completed >= 0) {
    hsem_trigger(completed == 0 ? WRITE_HSEM_ID_0 : WRITE_HSEM_ID_1);
  }
  // Start next region if available
  if (read_trigger_0 && !dac_output.is_busy()) { ... }
}
```

**Risk:** Changes the timing of HSEM signals. Currently the signal fires immediately after playback completes. With polling, there's a loop iteration delay. Probably negligible but needs testing.

**Estimated gain:** Frees M4 CPU for future work (effects, mixing). No immediate perf gain unless M4 has other work to do.
