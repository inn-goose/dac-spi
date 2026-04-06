---
name: Architecture Challenge - Could M4 Do Everything?
description: M4 can't access USB Serial (D1 domain) — this forces the M7+M4 split, but is there a workaround?
type: project
---

**Challenge:** The architecture split is forced by hardware: USB OTG is in D1 domain, managed by M7. M4 can't read Serial directly. So M7 must receive serial data and pass it to M4 via shared memory.

**What if M4 could read serial?**
- Single-core solution on M4: parse + play, no IPC needed
- All complexity of shared memory, HSEM, cache management disappears
- But: M4 at 240MHz vs M7 at 480MHz — M4 is slower

**Workaround: Raw byte pipe from M7 to M4.**
Instead of parsing on M7, M7 could be a dumb forwarder:
```
M7: Serial.readBytes() → write to shared memory (raw bytes) → HSEM
M4: read raw bytes from shared memory → parse → play
```

This moves ALL logic to M4. M7 is just a USB-to-shared-memory bridge. 

**Pros:**
- All application logic on one core — easier to debug
- No parser state split across cores
- M4 controls its own timing

**Cons:**
- M4 is slower (240MHz vs 480MHz) — but parsing is trivial
- M4 now has TWO jobs (parse + play) — back to the jitter problem
- Still need shared memory + HSEM for the byte pipe

**Verdict:** Not better than the current design. The parsing is so lightweight that where it runs doesn't matter. The current split (M7 parses, M4 plays) is clean and well-separated.
