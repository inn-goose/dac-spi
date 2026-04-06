---
name: Refactor - RingBuffer May Be Unnecessary
description: StreamingParser has a 32KB RingBuffer between Serial and parser — the parser could potentially read directly from Serial
type: project
---

**File:** `serial_streaming_lib.h:33-62` (RingBuffer) and `serial_streaming_lib.h:96-107` (loop)

**Problem:** The current flow is:
```
Serial HW buffer → Serial.readBytes(buf, 256) → RingBuffer (32KB) → parser pops byte-by-byte
```

The RingBuffer exists as a decoupling layer between serial reads and parsing. But `Serial.readBytes()` already reads into a local 256-byte buffer, which is then pushed byte-by-byte into the 32KB RingBuffer, which is then popped byte-by-byte by the parser.

**Question:** Why not parse directly from the 256-byte bulk buffer?

```cpp
void loop() {
  int avail = Serial.available();
  if (avail > 0) {
    uint8_t buf[256];
    int count = min(avail, (int)sizeof(buf));
    Serial.readBytes(buf, count);
    parse(buf, count);  // parse directly, no ring buffer
  }
}
```

**Why the RingBuffer might still be needed:**
1. If `parse()` can't keep up with incoming data, the ring buffer provides backpressure buffering
2. If Serial arrives faster than parsing (unlikely since parsing is just byte comparisons and memory writes)
3. The Arduino Serial already has its own internal buffer (~512 bytes on GIGA)

**Why it might not be needed:**
1. The parser is fast — each byte is just a switch/case with a memory write
2. Arduino Serial has its own HW + SW buffer
3. 32KB of RAM is significant on a microcontroller
4. The push-then-pop adds overhead

**Risk:** High if Serial can burst faster than the parser processes. Need to measure actual throughput. The USB CDC can burst at ~1.5MB/s, parser at worst writes one byte to shared memory per byte received — should be fast enough.

**Estimated gain:** -32KB RAM, fewer function calls per byte.
