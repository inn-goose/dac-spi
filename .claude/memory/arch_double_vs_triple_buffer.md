---
name: Architecture Challenge - Double Buffering May Not Be Enough
description: 2 regions means M7 must finish writing before M4 finishes playing — USB latency spikes could cause audio gaps
type: project
---

**Challenge:** With double-buffering, the pipeline is:
```
Time →
M7: [write R0] [write R1] [write R0] [write R1]
M4:            [play R0]  [play R1]  [play R0]
```

M7 must complete writing R1 before M4 finishes playing R0. If M7 is delayed (USB latency spike, Python GC pause, OS scheduling), M4 has nothing to play → audio gap.

**How much runway?** At 44.1kHz stereo, 16368 samples per region = ~185ms of audio. M7 needs to receive and write 32KB of serial data in that time. At 2Mbaud, 32KB takes ~131ms. So there's ~54ms of slack. Seems fine.

**But:** USB CDC latency can spike to 10-50ms under OS load. Python's GC can pause for 10-20ms. Combined with CLI ACK round-trip, the slack shrinks. A bad combination of delays could cause a gap.

**Triple buffering:**
- 3 × 21KB regions (63KB, fits in 64KB D4 SRAM)
- ~10,500 samples per region = ~119ms of audio each
- M7 can be one full region behind — ~238ms of runway instead of ~185ms
- But each region is smaller → more HSEM transitions → more overhead

**Alternative: Larger packets, fewer transitions.**
- Current: packet_size=8000 (from CLI default), region=32KB
- A packet doesn't fill a full region (8000 × 2 = 16KB of a 32KB region)
- Could increase packet_size to ~16000 to fill regions completely
- Fewer packets = fewer ACK round-trips = more time for each

**Verdict:** Double buffering works now. Triple buffering is insurance for harder scenarios (higher sample rates, smaller packets, loaded host). The 64KB SRAM constraint makes triple buffering tight but feasible.
