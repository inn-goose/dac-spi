---
name: Architecture Challenge - Stop-and-Wait ACK Model Limits Throughput
description: CLI sends one packet, waits for ACK, sends next — round-trip latency wasted, could pipeline with sliding window
type: project
---

**Challenge:** The CLI does strict stop-and-wait flow control:
```
CLI: send packet → wait for ACK byte → send next packet → wait for ACK...
```

Each round-trip includes: Python serial write → USB transfer → Arduino parse → Arduino ACK → USB transfer → Python serial read. USB latency alone is typically 1-2ms per direction. So there's 2-4ms of dead time between every packet.

At 8000 samples/packet, each packet represents:
- Mono 44.1kHz: 181ms of audio
- Stereo 44.1kHz: 90ms of audio

So 2-4ms overhead on 90ms is ~3-4%. Tolerable. But with smaller packets (for lower latency), the overhead percentage grows.

**Potential improvement: Pipelining.** The Arduino has 2 shared memory regions. The CLI could send packet N+1 while M4 plays packet N, without waiting for the ACK of packet N. The ACK would serve as backpressure — if no ACK arrives, the CLI pauses.

```
CLI: send pkt0 → send pkt1 → wait ACK0 → send pkt2 → wait ACK1 → ...
```

This overlaps USB transfer time with playback time.

**Why it might not matter:** With 8000-sample packets at 44.1kHz stereo, each packet is 90ms of audio. The 2-4ms USB round-trip is dwarfed. The bottleneck is playback speed, not transfer speed.

**When it WOULD matter:** If packet sizes shrink (for lower latency) or sample rates increase (96kHz+).

**Risk:** Medium. Pipelining adds complexity to both CLI and Arduino. The Arduino parser already handles this naturally (it acquires whichever region is free), but the CLI ACK handling would need to become asynchronous.
