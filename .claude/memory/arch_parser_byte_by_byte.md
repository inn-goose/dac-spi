---
name: Architecture Challenge - Parser Processes Payload Byte-by-Byte
description: In WAIT_PAYLOAD state, parser could bulk-copy remaining bytes from ring buffer instead of popping one at a time
type: project
---

**Challenge:** Once the parser enters WAIT_PAYLOAD, it knows exactly how many bytes it needs (`_meta_packet_size * 2 - _payload_index`). But it still processes them one byte at a time through the switch/case:

```cpp
while (_ring_buffer.pop(&b)) {
  switch (_state) {
    // ... header states ...
    case WAIT_PAYLOAD:
      if (_payload_index & 1) {
        _samples_dest[_payload_index >> 1] = (int16_t)(_current_low_byte | ((uint16_t)b << 8));
      } else {
        _current_low_byte = b;
      }
      _payload_index++;
      // ...
  }
}
```

**Potential optimization:** Add a bulk drain mode for WAIT_PAYLOAD. When in this state, calculate how many bytes the ring buffer has and how many the payload needs, then memcpy the minimum:

```cpp
if (_state == WAIT_PAYLOAD) {
  size_t remaining = _meta_packet_size * 2 - _payload_index;
  size_t available = _ring_buffer.contiguous_available();
  size_t chunk = min(remaining, available);
  memcpy((void*)((uint8_t*)_samples_dest + _payload_index), 
         _ring_buffer.read_ptr(), chunk);
  _ring_buffer.advance(chunk);
  _payload_index += chunk;
  // check completion...
}
```

**Challenges:**
1. RingBuffer doesn't expose `contiguous_available()` or `read_ptr()` — would need to add these
2. The ring buffer wraps around — contiguous bytes may be split at the boundary
3. memcpy with volatile destination — same old problem, need volatile-aware copy
4. This only helps if the ring buffer has many bytes ready (which depends on USB burst timing)

**Combined with RingBuffer elimination:** If the RingBuffer is removed entirely (see refactor_ringbuffer_unnecessary.md), parsing could work directly on the 256-byte bulk read buffer. In WAIT_PAYLOAD state, just memcpy from `buf[current_pos]` to shared memory.

**Estimated gain:** Could reduce per-byte overhead by 10-50x for the payload portion. Header bytes (4-5 per packet) still go through the state machine.
