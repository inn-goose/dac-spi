---
name: Refactor - Duplicate M4 Region Branches
description: core_m4.h has two nearly identical if/else blocks for region 0 and region 1 — should be a single parameterized path
type: project
---

**File:** `core_m4.h:76-121`

**Problem:** The two branches for region 0 and region 1 are copy-pasted with only the IDs changed:
```cpp
if (read_memory_region_id == 0 && read_trigger_0) {
  read_trigger_0 = false;
  SharedRegionHeader* header = get_region_header(0);
  // ... identical logic ...
  hsem_trigger(WRITE_HSEM_ID_0);
  read_memory_region_id = 1;
} else if (read_memory_region_id == 1 && read_trigger_1) {
  read_trigger_1 = false;
  SharedRegionHeader* header = get_region_header(1);
  // ... identical logic ...
  hsem_trigger(WRITE_HSEM_ID_1);
  read_memory_region_id = 0;
}
```

**Potential fix:** Use arrays for triggers and HSEM IDs:
```cpp
volatile bool* read_triggers[2] = { &read_trigger_0, &read_trigger_1 };
uint32_t write_hsem_ids[2] = { WRITE_HSEM_ID_0, WRITE_HSEM_ID_1 };

if (*read_triggers[read_memory_region_id]) {
  *read_triggers[read_memory_region_id] = false;
  int rid = read_memory_region_id;
  SharedRegionHeader* header = get_region_header(rid);
  dac_output.start_playback(header->n_channels, header->sample_rate,
                            header->samples_count, get_region_samples(rid), rid);
  while (dac_output.is_busy()) __NOP();
  hsem_trigger(write_hsem_ids[rid]);
  read_memory_region_id = 1 - rid;
}
```

**Risk:** Low. Pure refactoring, same logic. Array indexing adds negligible overhead.
