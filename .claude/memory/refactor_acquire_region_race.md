---
name: Refactor - Region Acquire Has Potential Race Condition
description: _acquire_region() reads two volatile bools non-atomically — both could become true between checks, causing wrong region selection
type: project
---

**File:** `serial_streaming_lib.h:283-311`

**Problem:** The `_acquire_region()` function has a TOCTOU pattern:
```cpp
void _acquire_region() {
  if (*_region_available[_current_region_id]) {
    *_region_available[_current_region_id] = false;
    return;
  }
  int other = 1 - _current_region_id;
  if (*_region_available[other]) {
    *_region_available[other] = false;
    _current_region_id = other;
    return;
  }
  // Both busy - spin
  while (!*_region_available[0] && !*_region_available[1]) { __NOP(); }
  // One freed up
  if (*_region_available[0]) {
    *_region_available[0] = false;
    _current_region_id = 0;
  } else {
    *_region_available[1] = false;
    _current_region_id = 1;
  }
}
```

After the spin-wait exits, both `_region_available[0]` and `_region_available[1]` could be true (if M4 freed both while M7 was spinning). The code takes region 0 in that case, which is fine. But between checking `_region_available[0]` and setting it to false, the HSEM ISR could fire and set it. Since the ISR only sets to `true` (never false), this is actually safe — worst case we miss the update and take the region anyway.

**Actual issue:** The volatile reads are not atomic 32-bit operations — but on Cortex-M7, reading a `bool` (1 byte or 4 bytes depending on ABI) IS atomic. So this is likely fine.

**Real concern:** The function doesn't disable interrupts during the check-and-set. An HSEM interrupt could fire between reading `*_region_available[0]` as true and setting it to false. But since the ISR only writes `true`, not `false`, the worst case is a redundant write — harmless.

**Verdict:** This is actually safe as-is, but the reasoning is subtle. Adding a comment explaining why would help. No code change needed.

**Risk:** None (safe as-is). Documentation improvement only.
