---
name: Refactor - CLI Metadata struct.pack Format Mismatch
description: cli.py packs metadata with 'I' for sample_rate (unsigned) and 'H' for packet_size, but Arduino reads them as uint32_t and uint16_t — matches, but total_samples is packed as 'I' (4 bytes) while the format table says offset 8, size 4
type: project
---

**File:** `dac_spi_cli/cli.py:101-107`

**Current pack format:**
```python
payload = struct.pack('<BBIHIB19x',
    n_channels,        # B = uint8  (offset 0)
    bits_per_sample,   # B = uint8  (offset 1)
    sample_rate,       # I = uint32 (offset 2)
    packet_size,       # H = uint16 (offset 6)
    total_samples,     # I = uint32 (offset 8)  ← WRONG: this is packed as 'I' but preceded by 'H', so offset is 8? Let me check...
    int(debug))        # B = uint8  (offset 12)
```

Wait, struct.pack('<BBIHIB19x'):
- B = 1 byte (offset 0)
- B = 1 byte (offset 1)
- I = 4 bytes (offset 2)
- H = 2 bytes (offset 6)
- I = 4 bytes (offset 8)
- B = 1 byte (offset 12)
- 19x = 19 pad bytes (offset 13-31)
Total = 32 bytes ✓

**Arduino side** (`serial_streaming_lib.h:192-204`):
```cpp
_meta_n_channels = _meta_buf[0];
_meta_bits_per_sample = _meta_buf[1];
_meta_sample_rate = (uint32_t)_meta_buf[2] | ((uint32_t)_meta_buf[3] << 8) | ...;
_meta_packet_size = (uint16_t)_meta_buf[6] | ((uint16_t)_meta_buf[7] << 8);
_meta_total_samples = (uint32_t)_meta_buf[8] | ...;
_meta_debug = _meta_buf[12];
```

**Verdict:** The format actually matches. No bug here. But the manual byte-by-byte parsing on the Arduino side is fragile — if someone changes the Python format string, the Arduino offsets break silently.

**Potential fix:** Define the metadata as a packed struct on both sides, or at least use named offset constants:
```cpp
static constexpr int META_OFF_NCHANNELS = 0;
static constexpr int META_OFF_BITS = 1;
static constexpr int META_OFF_SAMPLERATE = 2;
// etc.
```

**Risk:** Low. Pure maintainability improvement.
