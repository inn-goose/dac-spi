---
name: Refactor - CLI Already Has Good Optimization but Debug Path Is Slower
description: cli.py debug mode uses send_raw_data() with full ACK parsing, non-debug uses raw=True — different code paths may mask bugs
type: project
---

**File:** `dac_spi_cli/cli.py:166-199`

**Problem:** Two completely separate loops for debug vs non-debug:

Debug mode (line 167-181): calls `dac_spi.stream_frame(packet_buf)` which goes through `send_raw_data(data, raw=False)` → reads ACK as string via `_read_raw_response()`.

Non-debug mode (line 183-199): calls `dac_spi.stream_frame(packet_buf, raw=True)` which goes through `send_raw_data(data, raw=True)` → reads 1 raw byte.

**Issue:** The `raw=True` path reads `self.serial.read(1)` directly. The `raw=False` path reads via `_read_raw_response()` which also calls `self.serial.read(1)` but then `.decode()`s it. Both read 1 byte, but the non-raw path adds string decode overhead and wraps in a tuple.

**More importantly:** If the Arduino sends more than 1 byte as ACK (or sends nothing), the behavior differs between paths. `raw=True` just reads 1 byte and ignores its value. `raw=False` checks for None and raises an error.

**Potential fix:** Unify the paths. Use one loop, conditionally print timing. The stream_frame call should always use raw=True for speed:
```python
for i in range(num_packets):
    # ... fill packet_buf ...
    ts = time.monotonic() if debug else 0
    dac_spi.stream_frame(packet_buf, raw=True)
    if debug:
        print(f"packet {i+1}/{num_packets}: {time.monotonic()-ts:.04f} sec")
```

**Risk:** Low. Simplification.
