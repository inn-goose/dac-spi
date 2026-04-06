---
name: Refactor - Payload Bytes Could Skip int16 Assembly
description: StreamingParser assembles int16 from byte pairs, but LE wire format matches LE memory — could write raw bytes directly
type: project
---

**File:** `serial_streaming_lib.h:220-228`

**Problem:** The parser assembles int16 values from byte pairs using `_current_low_byte`:
```cpp
case WAIT_PAYLOAD:
  if (_payload_index & 1) {
    _samples_dest[_payload_index >> 1] = (int16_t)(_current_low_byte | ((uint16_t)b << 8));
  } else {
    _current_low_byte = b;
  }
  _payload_index++;
```

But both the wire format (LE int16) and the Cortex-M7 (LE) use the same byte order. So raw bytes written sequentially to memory ARE valid int16 values without any assembly.

**Potential fix:** Write raw bytes directly to the region:
```cpp
case WAIT_PAYLOAD:
  ((volatile uint8_t*)_samples_dest)[_payload_index] = b;
  _payload_index++;
```

This eliminates:
- The `_current_low_byte` member variable
- The odd/even branch
- The bit shift and OR operations

**Why this wasn't done:** The `_samples_dest` is `volatile int16_t*` (from `get_region_samples()`). Writing bytes to it requires casting to `volatile uint8_t*`. This is valid C++ but the cast looks suspicious. Also, the original code predates the shared memory refactor and was writing to a local `_samples_buffer`.

**Risk:** Low. Both architectures are LE. The cast is safe. But it relies on the assumption that the wire format never changes to BE.

**Estimated gain:** Eliminates a branch and a shift+OR per byte in the hottest loop of M7.
