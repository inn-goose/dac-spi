---
name: Refactor - Shared Region Header Written Before Payload Complete
description: StreamingParser writes the header to shared memory immediately when TYPE_DATA is received, before any samples arrive
type: project
---

**File:** `serial_streaming_lib.h:167-179`

**Problem:** When a data packet type is detected, the parser immediately writes the header:
```cpp
} else if (b == _TYPE_DATA) {
  if (!_metadata_received) {
    _state = WAIT_HEADER1;
  } else {
    _acquire_region();
    _region_acquired = true;
    SharedRegionHeader* header = get_region_header(_current_region_id);
    header->n_channels = _meta_n_channels;
    header->sample_rate = _meta_sample_rate;
    header->samples_count = _meta_packet_size;
    // ... then enters WAIT_PAYLOAD
  }
}
```

The header is written to shared memory BEFORE any payload bytes arrive. If the packet is corrupted or the connection drops, the header is already there with potentially stale/wrong data. The region is acquired but may never be committed.

**Current mitigation:** `reset()` releases the acquired region without HSEM signal:
```cpp
if (_region_acquired && _region_available[_current_region_id]) {
  *_region_available[_current_region_id] = true;
}
```

So M4 never sees the partial data. But the header in shared memory contains valid-looking data from the aborted packet.

**Is this actually a problem?** No — M4 only reads the region after HSEM signal, which only fires in `_commit_region()`. The stale header doesn't matter because M4 won't look at it until the next valid write + commit overwrites it.

**Verdict:** Safe as-is. But writing the header in `_commit_region()` (just before the cache clean and HSEM trigger) would be more defensive. The data is known-good at that point.

**Risk:** None (current code is safe). Defensive improvement only.
