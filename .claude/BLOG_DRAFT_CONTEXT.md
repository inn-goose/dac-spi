# DAC SPI Performance Refactoring — Blog Post Context

## How to use this document

This is a reconstructed timeline of the performance refactoring of the DAC SPI project. It covers all the problems discovered, hypotheses tested, dead ends hit, and solutions applied. It is organized into 3 natural post themes.

---

## The Starting Point

The project is an external audio card / DAC music player built around:
- **Arduino GIGA R1** (STM32H747, dual-core: Cortex-M7 @ 480MHz + Cortex-M4 @ 240MHz)
- **AD1860 DAC chip** (18-bit, driven via software SPI)
- **PC CLI** (Python) that streams WAV files over USB serial

The initial version (`first player version` commit) was a single-core sketch playing hardcoded PCM samples at 32kHz from a compiled header file. The `add streaming mode beta` commit introduced serial streaming with a JSON-RPC protocol and a binary streaming protocol. Everything ran on one core. Sample rate was 8kHz.

**The goal:** achieve 44.1kHz stereo streaming (176,400 samples/sec) — CD quality — over USB serial from a PC.

**The bottleneck:** at 8kHz, there were already ~50% idle gaps between packets. The system couldn't keep up. Three separate subsystems needed optimization.

---

# Post 1: Serial Parser — From 96KB of Buffers to 1 Byte

**Theme:** Taking a working-but-wasteful streaming parser and stripping it down to the bone.

## The Original Parser

The `StreamingParser` class parsed a binary protocol: `[0xAB][0xCD][N_CHANNELS][LEN_LO][LEN_HI][PAYLOAD...]` where payload was `LEN * int16_t` (little-endian).

### Problem 1: Triple buffering eating all the RAM

The original parser used three separate buffers:
- `RingBuffer` (32KB) — intermediate storage for incoming serial bytes
- `_payload_bytes[32000]` (32KB) — raw bytes accumulated during parsing
- `_samples_buffer[16000]` (32KB) — converted int16 samples

**Total: ~96KB** of static buffers for a microcontroller. And the data was copied twice: once into `_payload_bytes`, then batch-converted into `_samples_buffer` after the packet was complete.

### Solution: Inline int16 assembly

Instead of collecting all raw bytes and then converting, the parser assembles int16 values on the fly using a single `_current_low_byte` variable (1 byte). When the even (low) byte arrives, it gets stashed. When the odd (high) byte arrives, the two are combined into an int16 and written directly to the output.

```cpp
// Before: 32KB payload buffer + 32KB samples buffer + batch conversion
_payload_bytes[_payload_index++] = b;
if (done) {
  for (uint32_t i = 0; i < _samples_count; i++)
    _samples_buffer[i] = (int16_t)(_payload_bytes[2*i] | (_payload_bytes[2*i+1] << 8));
}

// After: 1 byte stash, direct write
if (_payload_index & 1) {
  _samples_buffer[_payload_index >> 1] = (int16_t)(_current_low_byte | ((uint16_t)b << 8));
} else {
  _current_low_byte = b;
}
```

**Result: Eliminated ~32KB of RAM and removed the post-parse conversion pass entirely.**

### Problem 2: Timeout logic was checking at the wrong time

The timeout was checked _before_ draining the ring buffer. If stale bytes were sitting in the buffer and fresh bytes arrived, the timeout would fire and reset, but then immediately parse those stale bytes in the same call. The timeout should fire _after_ the ring buffer is empty and the parser is stuck mid-packet.

### Solution: Move timeout after the parse loop

```cpp
// Before: timeout checked before parsing (wrong)
if (_state == WAIT_PAYLOAD && millis() - ts > TIMEOUT) reset();
while (ring.pop(&b)) { ... }

// After: timeout checked after buffer drained (correct)
while (ring.pop(&b)) { ... }
if (mid_packet && millis() - ts > TIMEOUT) reset();
```

### Problem 3: Header re-sync lost valid sequences

If the byte stream contained `0xAB 0xAB 0xCD`, the parser would:
1. See first `0xAB` → move to `WAIT_HEADER2`
2. See second `0xAB` → not `0xCD`, reset to `WAIT_HEADER1`
3. See `0xCD` → ignored, waiting for `0xAB`

The valid `0xAB 0xCD` pair starting at byte 2 was missed.

### Solution: Stay in WAIT_HEADER2 when seeing 0xAB

```cpp
case WAIT_HEADER2:
  if (b == _HEADER2) _state = WAIT_NCHANNELS;
  else if (b == _HEADER1) { /* stay — re-sync */ }
  else _state = WAIT_HEADER1;
```

### Problem 4: Zero-length packets caused garbage callbacks

A `_samples_count` of 0 passed validation, entered `WAIT_PAYLOAD`, and the completion check `_payload_index >= 0 * 2` was immediately true on the next byte — eating one byte and firing the callback with uninitialized data.

### Solution: Reject zero-length and non-channel-divisible counts

```cpp
if (_samples_count == 0 || _samples_count > MAX || (_samples_count % _n_channels) != 0) {
  reset();
}
```

### Problem 5: Byte-by-byte serial reads

`Serial.read()` was called in a loop, one byte at a time. Each call has overhead.

### Solution: Bulk reads with `Serial.readBytes()`

```cpp
// Before:
while (Serial.available()) _ring_buffer.push(Serial.read());

// After:
int avail = Serial.available();
if (avail > 0) {
  uint8_t buf[256];
  Serial.readBytes(buf, min(avail, 256));
  for (int i = 0; i < count; i++) _ring_buffer.push(buf[i]);
}
```

### Cleanup items
- `unsigned long long last_byte_read_ts` → `unsigned long` (matches `millis()` return, saves 4 bytes)
- Removed duplicate `_state` initialization (in-class + constructor list)

## The Big Endianness Insight (later, during dual-core refactor)

When the parser was later refactored to write directly to shared memory (see Post 3), an important realization emerged: since both the wire format and Cortex-M7 are little-endian, raw payload bytes can be written sequentially to the memory region without any byte-pair assembly at all. The `_current_low_byte` trick becomes unnecessary — you just write each byte as it arrives:

```cpp
region_ptr[HEADER_SIZE + _payload_index] = b;
```

The int16 values magically appear correctly aligned in memory because LE wire order = LE memory order.

---

# Post 2: From Bit-Banging to Hardware SPI and Direct GPIO

**Theme:** Replacing Arduino abstractions with bare-metal STM32 register access for the DAC output path.

## The Original DAC Driver

The first version (`dac_spi.h` / `PcmPlayer`) used software bit-banging through `digitalWrite()`:
- For each 16-bit sample: 16 data bits + 16 clock toggles + latch toggle = ~50 `digitalWrite()` calls
- Stereo doubled this to ~100 calls per sample
- Each `digitalWrite()` costs ~5μs on Arduino (pin validation, port lookup, etc.)
- **Total: ~550μs per stereo sample** — theoretical max ~1.8kHz

The sketch even included a comment calculating this:
> "one 16bit DAC channel costs about 16 + 16 * 2 + 2 = 50 digitalWrite... 100 * 5us + 10% = 550us"

### Problem 1: GPIO abstraction overhead

`digitalWrite()` does pin number validation, looks up the port/pin mapping, and performs thread-safety operations on every single call. For a bit-banged SPI signal running at audio rates, this is devastating.

### Solution: Direct GPIO register access via BSRR

The STM32H7 GPIO peripheral has a Bit Set/Reset Register (BSRR) that sets or clears pins in a single CPU cycle:

```cpp
// Before: ~5μs per call
digitalWrite(latch_pin, HIGH);

// After: ~1 cycle (< 10ns)
latch_port->BSRR = latch_mask;          // HIGH
latch_port->BSRR = latch_mask << 16;    // LOW
```

The pin-to-port/mask mapping is done once at setup via `arduinoToGpio()`, then cached for the hot path.

**Estimated speedup: 10-20x on GPIO operations alone.**

### Problem 2: Software SPI fundamentally can't keep up

Even with direct GPIO, bit-banging SPI has limits. Each bit requires a data write + clock toggle + clock toggle = 3 register writes. At 16 bits per sample, that's 48 register operations minimum per channel.

### Solution: Hardware SPI6 on the M4 core

The STM32H747's SPI6 peripheral is in the D3 power domain (always-on) and accessible from both cores. It can clock out 16 bits in a single hardware transaction:

```cpp
static void spi6_init() {
  hspi6.Instance = SPI6;
  hspi6.Init.DataSize = SPI_DATASIZE_16BIT;
  hspi6.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_16;
  // ... Mode 0, MSB first, TX-only
  HAL_SPI_Init(&hspi6);
}
```

The hot path became:
1. Latch HIGH (one register write)
2. Enable SPI + start transfer (two register writes)
3. Write 16-bit data to TX FIFO (one register write)
4. Wait for EOT flag
5. Clear flags + disable SPI
6. Latch LOW (one register write)

This replaced 50+ GPIO toggles with a handful of register operations plus hardware-timed serial clock generation.

**Key challenge:** PB3 (SPI6_SCK) defaults to JTAG TRACESWO after reset. Had to explicitly release it: `DBGMCU->CR &= ~DBGMCU_CR_DBG_TRACECKEN;`

### Problem 3: Interrupt-driven playback with polling

The original design used `mbed::Ticker` to set a flag, then polled it in `loop()`:

```cpp
void sample_rate_timer_callback() { sample_rate_timer_tick = true; }
void loop() {
  if (sample_rate_timer_tick) {
    sample_rate_timer_tick = false;
    pcm_player_L.tick(); pcm_player_L.draw();
  }
}
```

This added jitter — if `loop()` was busy with serial parsing, the tick could be delayed.

### Solution: Direct ISR playback

The Ticker ISR now directly calls `draw_and_tick()` which does the SPI transmission:

```cpp
void _isr() {
  if (!_playing) return;
  _pcm_player_L->draw_and_tick();
  _pcm_player_R->draw_and_tick();
  if (!_pcm_player_L->is_playing() && !_pcm_player_R->is_playing()) {
    _playing = false;
    _completed_region = _current_region;
  }
}
```

The Ticker runs continuously once started (no attach/detach overhead between buffers). During idle ticks it just returns early.

### Problem 4: Unnecessary buffer copies

The original flow had 3 copies:
```
Serial → RingBuffer → SharedMem → DacOutput._left_buffer → PcmPlayer._samples_buffer
```

### Solution: PcmPlayer reads directly from shared memory

`PcmPlayer` now stores a `volatile int16_t*` that points straight into the shared memory region. No intermediate buffers. Stereo de-interleaving is handled via stride/offset parameters rather than a separate copy:

```cpp
void play_from_interleaved(volatile int16_t* src, size_t frames, int channel, int n_channels) {
  _samples = src;       // direct pointer to shared memory
  _stride = n_channels; // skip every other sample
  _offset = channel;    // 0 for L, 1 for R
}
```

**Result: Eliminated 32KB+ of intermediate buffers and 2 copy operations per packet.**

---

# Post 3: Dual-Core Architecture — M7 Receives, M4 Plays

**Theme:** Splitting the workload across two cores with shared memory, hardware semaphores, and all the cache coherency traps.

## Why Dual-Core?

The Arduino GIGA's STM32H747 has two cores:
- **Cortex-M7** (480MHz) — fast, has D-cache, runs the main sketch
- **Cortex-M4** (240MHz) — no D-cache, can access D3/D4 peripherals

Running serial parsing and DAC output on the same core meant they competed for CPU time. Serial parsing could delay DAC sample output, causing audible glitches. Separating them guarantees real-time DAC output on M4 regardless of serial activity on M7.

## The Shared Memory Architecture

### Memory layout: D4 SRAM (SRAM4)

Two 32KB regions in the always-on D4 domain at `0x38000000`:
```
0x38000000 - 0x38007FFF: Region 0 (32KB)
0x38008000 - 0x3800FFFF: Region 1 (32KB)
```

Each region has a 32-byte cache-aligned header:
```cpp
struct __attribute__((aligned(32))) SharedRegionHeader {
  uint32_t n_channels;     // 1 or 2
  uint32_t sample_rate;    // Hz
  uint32_t samples_count;  // total int16 samples
};
// Samples start at offset 32, leaving room for 16,368 int16 values
```

### Double-buffering protocol

1. M7 writes samples into Region 0, triggers HSEM to notify M4
2. M7 immediately starts writing Region 1
3. M4 reads Region 0 header, starts DAC playback directly from shared memory
4. M4 finishes Region 0, triggers HSEM back to M7 ("Region 0 free")
5. Meanwhile M7 finishes Region 1, triggers HSEM
6. And so on, ping-ponging between regions

This ensures M7 can always be receiving serial data while M4 is playing audio.

## Hardware Semaphores (HSEM) — The IPC Mechanism

### What they are

The STM32H7 has 32 hardware semaphores. They're not mutexes — they're lightweight notification primitives. The key operation is take-then-release, which generates an interrupt on the other core.

### The signaling scheme

Four semaphore IDs:
- **28, 29 (WRITE_HSEM_ID_0/1):** M4 → M7 notifications ("region N is free, you can write")
- **30, 31 (READ_HSEM_ID_0/1):** M7 → M4 notifications ("region N has data, start playing")

### Dead end: Direct register access for HSEM trigger

An optimization was attempted — bypassing HAL and using direct register access:

```cpp
// Attempted optimization (FAILED):
HSEM->RLR[hsem_id];      // 1-step take via Read Lock Register
HSEM->R[hsem_id] = 0;    // release
```

**This didn't fire the interrupt.** The `RLR` read is a _try_-take — if it fails (another core holds it), no lock transition occurs, so the release doesn't generate a notification. The notification fires on the **lock-to-unlock transition**, and without a successful lock, writing 0 is a no-op.

The original `HAL_HSEM_FastTake` uses a 2-step take (write to `R[id]` with core ID and lock bit, read back to verify), which always establishes the lock. **Lesson: FastTake is required for interrupt generation, not optional.**

### HSEM callback optimization (accepted)

The HSEM callback was optimized with batch operations:

```cpp
// Before: two independent check-clear-reactivate sequences
if (SemMask & mask_0) { clear(mask_0); activate(mask_0); trigger_0 = true; }
if (SemMask & mask_1) { clear(mask_1); activate(mask_1); trigger_1 = true; }

// After: one combined clear+reactivate, then set flags
uint32_t relevant = SemMask & (mask_0 | mask_1);
if (!relevant) return;
__HAL_HSEM_CLEAR_FLAG(relevant);
HAL_HSEM_ActivateNotification(relevant);
if (relevant & mask_0) trigger_0 = true;
if (relevant & mask_1) trigger_1 = true;
```

Using `constexpr` masks means the compiler pre-computes them. The early-return avoids touching any HSEM registers when the callback is for an unrelated semaphore.

## Cache Coherency — The Hardest Bug

### The fundamental problem

The M7 has a write-back data cache. When M7 writes to shared memory, the data may sit in cache and never reach actual RAM. M4 has no cache, so it reads directly from RAM — and sees stale/zero data.

### The rules

1. **After M7 writes shared memory → clean D-cache** (flush cache to RAM)
2. **Before M7 reads data written by M4 → invalidate D-cache** (discard stale cache)
3. M4 doesn't need cache management (no D-cache on M4)

### Cache line alignment matters

Cache operations work on 32-byte cache lines. The code aligns to cache line boundaries:

```cpp
static inline void cleanDCache(void* addr, size_t size) {
  uint32_t start = (uint32_t)addr & ~0x1FU;        // align down to 32-byte boundary
  uint32_t length = /* ... round up to 32 bytes */;
  SCB_CleanDCache_by_Addr((uint32_t*)start, length);
  __DMB();  // data memory barrier
}
```

### Evolution of memory barriers

The original code used `__DSB(); __ISB();` (Data Synchronization Barrier + Instruction Synchronization Barrier) after every cache operation. This was refined:
- `__ISB()` was removed — it flushes the instruction pipeline and is only needed when executing code from cleaned memory, not for data operations
- `__DSB()` was downgraded to `__DMB()` — a Data Memory Barrier is sufficient for ensuring memory ordering without the full pipeline stall of DSB

### Contiguous region optimization

The original code with 4 separate 16KB regions called `cleanDCache()` four times at startup. Since the regions are contiguous in memory (`0x38000000` through `0x3800FFFF`), a single cache clean covering the full 64KB is equivalent and faster:

```cpp
// Before: 4 cache cleans
cleanDCache(region_0_0, 16*1024);
cleanDCache(region_0_1, 16*1024);
cleanDCache(region_1_0, 16*1024);
cleanDCache(region_1_1, 16*1024);

// After: 1 cache clean
cleanDCache(region_0, 64*1024);
```

## The ChatGPT Dual-Core Debugging Log

Before the Claude conversations, there was a series of debugging sessions (recorded in `chatgpt giga_dual_core_log.md`) that established the foundational dual-core knowledge:

1. **HSEM IRQ handler conflicts:** ArduinoCore-mbed already defines `HSEM2_IRQHandler` and `HAL_HSEM_FreeCallback`. Can't override them — must use the HAL callback mechanism.

2. **HSEM notifications are one-shot:** After the callback fires, you must re-arm it with `HAL_HSEM_ActivateNotification()`. Without this, it fires once and never again.

3. **Shared memory addressing:** Different cores see different linker layouts. Using `.RAM_D2` section placement resulted in different addresses on each core. Fixed by using hardcoded addresses in D4 SRAM.

4. **volatile and memcpy don't mix:** `memcpy()` is not guaranteed to respect volatile semantics. Need explicit volatile-aware copy functions.

5. **M4 boot:** Without RPC/OpenAMP, M4 must be booted manually with `HAL_RCCEx_EnableBootCore(RCC_BOOT_C2)`.

## The Region Acquire/Wait Pattern

The parser needs to acquire a region before writing to it. If both regions are busy (M4 still playing both), the parser spin-waits:

```cpp
void _acquire_region() {
  // Try current, then other, then spin-wait
  if (*_region_available[_current_region_id]) { /* take it */ return; }
  int other = 1 - _current_region_id;
  if (*_region_available[other]) { /* take it */ return; }
  // Both busy — spin
  while (!*_region_available[0] && !*_region_available[1]) __NOP();
}
```

This is the backpressure mechanism — if the PC sends faster than the DAC can play, the M7 blocks here, which stalls serial reads, which applies TCP-level backpressure through USB.

## The Protocol Evolution (v1 → v2)

The protocol grew from a simple 5-byte header to a 2-phase protocol:

**v1:** `[0xAB][0xCD][N_CH][LEN_LO][LEN_HI][payload]`
- Channel count and sample count per packet
- No sample rate info — hardcoded

**v2:** Separate metadata + data packets with a 4-byte common header:
- `[0xAB][0xCD][0xEF][TYPE]`
- TYPE=0x01: Metadata (32 bytes: channels, bits_per_sample, sample_rate, packet_size, total_samples, debug flag)
- TYPE=0x02: Data (payload = packet_size * 2 bytes)
- ACK: device sends "1" after each packet

This allowed the PC to negotiate format once, then stream pure data. The sample rate is now dynamic — the Ticker period is set from metadata rather than a compile-time constant.

---

# Summary: The Full Optimization Stack

| Layer | Before | After | Gain |
|-------|--------|-------|------|
| Serial reads | byte-by-byte `Serial.read()` | bulk `Serial.readBytes()` 256B chunks | 5-10x fewer calls |
| Parse buffers | 96KB (ring + payload + samples) | 32KB (ring only) + 1 byte stash | -64KB RAM |
| DAC output | bit-banged `digitalWrite()` | hardware SPI6 + direct GPIO | 10-20x faster |
| Playback timing | flag-polling in `loop()` | direct ISR via `mbed::Ticker` | zero jitter |
| Buffer copies | 3 copies per packet | 0 copies (direct shared memory pointer) | -32KB+ movement |
| Core utilization | single-core (M7 does everything) | M7 serial+parse, M4 DAC output | full separation |
| Memory barriers | `__DSB() + __ISB()` | `__DMB()` only | ~10 cycles/op |
| HSEM callbacks | per-semaphore clear+reactivate | batched with constexpr masks | fewer register ops |
| Cache cleans | per-region (4x) | contiguous (1x) | 3 fewer syscalls |
| Protocol | per-packet metadata | metadata once + pure data stream | less overhead |

---

# Dead Ends, Rollbacks, and Failed Hypotheses (Detailed)

This section catalogs every approach that was tried and either failed on the hardware, turned out to be unnecessary, or was rejected for complicating the design. These are the most valuable parts of the story — they represent real debugging time and non-obvious knowledge about the STM32H747 platform.

## Dead End 1: Direct HSEM Register Access (RLR)

**Hypothesis:** The HAL functions `HAL_HSEM_FastTake()` + `HAL_HSEM_Release()` have call overhead. We can bypass them with direct register access for maximum speed.

**What was tried:**
```cpp
// Proposed "faster" implementation:
static inline void hsem_trigger(const uint32_t hsem_id) {
  HSEM->RLR[hsem_id];      // 1-step take via Read Lock Register
  HSEM->R[hsem_id] = 0;    // release (write 0)
}
```

The `RLR` (Read Lock Register) is the STM32H7's "1-step" semaphore mechanism — a single read atomically tries to acquire the semaphore. This looked like the fastest possible path.

**What happened:** Semaphore notifications stopped firing. The M4 never received the interrupt. The system hung.

**Root cause analysis (multiple rounds of investigation):**

1. First hypothesis was that `RLR` read might not be generating the interrupt correctly. But the STM32 reference manual says it should.

2. The actual issue: `RLR` read is a **try-take** — it can **fail** if another core holds the semaphore. When it fails, the semaphore is never locked, so writing `0` to `R[id]` isn't a lock→unlock transition. The interrupt fires on the **transition**, not on the absolute state.

3. `HAL_HSEM_FastTake()` is different — it's a **2-step take** (write to `R[id]` with core ID and lock bit set, then read back to verify). The write to `R[id]` with the lock bit **always** locks the semaphore regardless of current state. This guarantees the subsequent `Release()` creates a lock→unlock transition.

4. Even more subtle: you might think "just check if the RLR read succeeded and retry" — but that defeats the purpose (you're back to multiple operations), and in the hot path you can't afford the retry loop.

**Resolution:** Reverted to `HAL_HSEM_FastTake()` + `HAL_HSEM_Release()`. The code even kept the RLR attempt as a comment for documentation:
```cpp
static inline void hsem_trigger(const uint32_t hsem_id) {
  // HSEM->RLR[hsem_id];          // 1-step take (read RLR) — DOES NOT WORK
  // HSEM->R[hsem_id] = 0;        // release (write 0)
  HAL_HSEM_FastTake(hsem_id);
  HAL_HSEM_Release(hsem_id, 0);
}
```

**Lesson:** On STM32H7, `HAL_HSEM_FastTake` is **required** for interrupt-based notification. The "fast" 1-step RLR path only works for polling-based mutual exclusion, not for interrupt generation.

---

## Dead End 2: Removing HAL_HSEM_FastTake Entirely

**Hypothesis:** Maybe we don't need the Take step at all — can we just call `HAL_HSEM_Release()` to trigger the notification?

**What was tried:** Calling `Release` without `FastTake` first.

**What happened:** System hangs. No interrupt fires.

**Root cause:** The notification fires on the Take→Release **transition**. If you never Take the semaphore, Release is a no-op — the semaphore was already in the "free" state, so there's no state change to generate an interrupt. This was explicitly documented in the CLAUDE.md as "NOT VALID" after testing:

> "FastTake is REQUIRED. The HSEM interrupt notification only fires on the Take→Release transition. Calling Release alone on an already-free semaphore does not trigger the interrupt. System hangs without FastTake."

**Lesson:** HSEM notification = state transition event, not absolute state. No transition = no interrupt. Period.

---

## Dead End 3: DMA for M4→M7 Memory Copies

**Hypothesis:** Using hardware DMA for memory-to-memory copies on the M4 core would be significantly faster than `memcpy()`.

**What was implemented:** Full DMA1 initialization on M4 with burst mode:
```cpp
static DMA_HandleTypeDef hdma_memcpy;

void m4_dma_init() {
  __HAL_RCC_DMA1_CLK_ENABLE();
  hdma_memcpy.Instance = DMA1_Stream0;
  hdma_memcpy.Init.Request = DMA_REQUEST_MEM2MEM;
  hdma_memcpy.Init.Direction = DMA_MEMORY_TO_MEMORY;
  hdma_memcpy.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
  hdma_memcpy.Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
  hdma_memcpy.Init.FIFOMode = DMA_FIFOMODE_ENABLE;
  hdma_memcpy.Init.FIFOThreshold = DMA_FIFO_THRESHOLD_FULL;
  hdma_memcpy.Init.MemBurst = DMA_MBURST_INC16;
  hdma_memcpy.Init.PeriphBurst = DMA_PBURST_INC16;
  HAL_DMA_Init(&hdma_memcpy);
}
```

**Why it was removed:** The DMA was being used for the M4's copy of shared memory region 0→1 (a test/verification step). When the architecture was refactored for actual audio playback, the M4 doesn't copy data at all — it reads directly from shared memory via `volatile int16_t*` pointers in the PcmPlayer. The DMA code was implemented, tested, worked correctly, but became irrelevant when the whole copy-and-verify flow was replaced by direct-read playback.

**Lesson:** Optimize the right thing. DMA would have been a valid optimization for the copy operation, but the better optimization was **eliminating the copy entirely**.

---

## Dead End 4: Removing __ISB() from Cache Operations

**Hypothesis:** `__ISB()` (Instruction Synchronization Barrier) is unnecessary after cache clean/invalidate operations when you're only dealing with data, not executing code from the cleaned region.

**Result:** This one **actually worked** — it's listed here because it was initially uncertain. The concern was that removing `__ISB()` might cause subtle ordering issues on the Cortex-M7's superscalar pipeline.

**The nuance:** `__ISB()` flushes the instruction pipeline. It's critical when you're modifying code memory (self-modifying code, DMA into instruction regions). For data-only operations like cleaning audio samples out to shared RAM, `__DSB()` alone is sufficient. The data will be visible to the other core after the DSB completes.

**Further refinement:** `__DSB()` was later downgraded to `__DMB()`. The difference:
- `__DSB()` — stalls the processor until all memory accesses complete
- `__DMB()` — ensures ordering but doesn't stall; subsequent data accesses wait, but computation can continue

For shared memory between cores, `__DMB()` is sufficient because we just need ordering guarantees, not a full pipeline stall.

**Lesson:** Memory barriers are a spectrum. Use the weakest one that gives the correctness guarantees you need. From weakest to strongest: `__DMB()` < `__DSB()` < `__ISB()`.

---

## Dead End 5: OpenAMP / RPC-based IPC

**Hypothesis:** Use Arduino's built-in `RPC.h` library for inter-core communication.

**What happened (from ChatGPT debugging log):**

1. RPC.h uses OpenAMP under the hood, which internally uses HSEM for its own mailbox
2. Trying to define your own `HSEM2_IRQHandler` or `HAL_HSEM_FreeCallback` causes linker errors: "multiple definition of `HSEM2_IRQHandler`" and "multiple definition of `HAL_HSEM_FreeCallback`"
3. ArduinoCore-mbed's `libmbed.a` already defines these handlers
4. You can't use both RPC.h and manual HSEM — they conflict

**Resolution:** Completely abandoned RPC.h and OpenAMP. This meant:
- **Must boot M4 manually:** `HAL_RCCEx_EnableBootCore(RCC_BOOT_C2)` — RPC.h did this silently
- **Must implement all IPC from scratch:** shared memory + manual HSEM handlers
- **Must handle M4 startup timing:** `__DSB(); __ISB();` after boot call

This was a pivotal decision — more work upfront, but gave complete control over the IPC mechanism with zero overhead.

**Lesson:** Arduino convenience libraries often own system-level resources (IRQ handlers, peripherals) that you can't share. If you need low-level control, you must go fully bare-metal on that subsystem.

---

## Dead End 6: .RAM_D2 Section Placement for Shared Memory

**Hypothesis:** Use linker section attributes to place shared buffers in a memory region accessible by both cores.

**What happened:** M7 saw the buffer at `0x240014A0`, M4 saw it at `0x10001728`. Each core has its own linker script with its own section layout. The "same" variable was at different physical addresses on each core.

**M4 read garbage/zeroes** because it was reading from its own private RAM, not from where M7 wrote.

**Resolution:** Hardcoded addresses in D4 SRAM (SRAM4):
```cpp
static constexpr uint32_t MEMORY_REGION_ADDRESS_0 = 0x38000000UL;
static constexpr uint32_t MEMORY_REGION_ADDRESS_1 = 0x38008000UL;
```

D4 SRAM (0x38000000–0x3800FFFF, 64KB) is:
- Accessible by both M7 and M4 at the same physical address
- In the "always-on" D3/D4 power domain
- Not cached by M4 (M4 has no D-cache)
- Cached by M7's D-cache (hence the need for cache management)

**Lesson:** On dual-core STM32, never rely on linker-placed variables for shared memory. Always use hardcoded addresses in a known shared region.

---

## Dead End 7: HSEM One-Shot Behavior (Silent Failure)

**Symptom:** Inter-core communication worked once, then never again.

**Root cause:** HSEM notifications are **one-shot**. After `HAL_HSEM_FreeCallback` fires, the notification is automatically disabled. You must re-arm it inside the callback:

```cpp
extern "C" void HAL_HSEM_FreeCallback(uint32_t SemMask) {
  // MUST re-arm, otherwise this callback never fires again
  __HAL_HSEM_CLEAR_FLAG(mask);
  HAL_HSEM_ActivateNotification(mask);
  // ... set trigger flags
}
```

This is not documented prominently in the STM32 HAL docs. It was discovered through trial-and-error debugging with ChatGPT.

**Lesson:** Most hardware notification mechanisms are one-shot. Always re-arm in the handler.

---

## Dead End 8: Using memcpy() with volatile Pointers

**Hypothesis:** `memcpy()` should work for copying data between volatile shared memory regions — it's just bytes.

**What happened:** Data corruption. Some values were stale, wrong, or zero.

**Root cause:** The C standard does not require `memcpy()` to respect `volatile` semantics. The compiler is free to optimize `memcpy()` using wide loads/stores, reorder accesses, or cache values in registers. When the source or destination is `volatile` (because it's shared memory), these optimizations can skip actual memory reads/writes.

**Resolution:** Built explicit volatile-aware copy functions using 32-bit operations:
```cpp
static inline void uint8_to_int16(volatile uint8_t* src, int16_t* dst, size_t count) {
  volatile uint32_t* src32 = (volatile uint32_t*)src;
  uint32_t* dst32 = (uint32_t*)dst;
  size_t word_count = count / 2;
  for (size_t i = 0; i < word_count; i++) {
    dst32[i] = src32[i];  // each read from volatile is guaranteed to happen
  }
}
```

The 32-bit reads are both volatile-safe (each read hits memory) and performant (2x int16 per operation).

**Lesson:** `volatile` and standard library functions don't mix. Write your own copy functions when dealing with hardware-mapped or shared memory.

---

## Dead End 9: Four Memory Regions → Two

**What changed:** The original architecture used **four** 16KB regions (0_0, 0_1, 1_0, 1_1) for a copy-and-verify pattern. M7 wrote to `X_0`, M4 copied to `X_1`, M7 compared. This was a testing/validation pattern.

**Why it was removed:** For actual audio playback, there's no need to copy and verify. M7 writes samples, M4 reads samples directly. So the architecture was simplified to **two** 32KB regions with double-buffering.

This freed up memory (same 64KB total, but each region is now 32KB = more samples per packet = fewer packet transitions = less overhead).

**Lesson:** Test harnesses and production architectures are different. Don't optimize the test code — redesign for the actual use case.

---

## Dead End 10: Packing Multiple Packets per Region

**Hypothesis (discussed in planning):** For maximum throughput, pack multiple small packets into a single 32KB region and flush when full.

**Why it was rejected:**
1. The PC controls packet size — it can send packets sized to fill the region
2. Packing complicates the header format (need per-packet headers within the region, or a count)
3. If `n_channels` differs between packets, you can't pack them (different interleaving)
4. The M4 needs to start playing ASAP — waiting to fill a region adds latency
5. What if a packet doesn't fit? Split across regions? That's very complex

**Decision:** One packet per region. The PC sends packets sized to fit. The last packet can be smaller (zero-padded). Simple, fast, no edge cases.

**Lesson:** Simplicity wins in embedded. The "optimal" packing solution has many edge cases that would each need testing on real hardware. The simple solution works and is debuggable.

---

## Dead End 11: Region Header Options (Byte-Aligned vs Cache-Aligned)

**Options considered:**

Option 1: PCM-friendly byte-aligned header (3 bytes):
```
offset 0: uint8_t  n_channels
offset 1: uint8_t  count_lo
offset 2: uint8_t  count_hi
offset 3: int16_t  samples[...]
```

Option 2: Cache-aligned struct (32 bytes):
```cpp
struct __attribute__((aligned(32))) SharedRegionHeader {
  uint32_t n_channels;
  uint32_t sample_rate;
  uint32_t samples_count;
};
// samples start at offset 32
```

**Initial preference was Option 1** ("let's use Option 1 since it's PCM friendly"). But the final implementation used **Option 2** because:
1. Cache-line alignment (32 bytes) avoids the risk of cleaning a cache line that overlaps both header and sample data during concurrent access
2. Using `uint32_t` fields avoids unaligned access penalties on Cortex-M7
3. The 32-byte overhead is negligible in a 32KB region (16,368 samples vs 16,382 — 0.08% difference)
4. The struct can be cast directly from the region pointer — no byte-by-byte parsing

**Lesson:** On cached systems, alignment to cache line boundaries isn't just performance — it's correctness. Misaligned structures can cause partial cache line cleanings that corrupt adjacent data.

---

## Dead End 12: Volatile-Aware Conversion Function Bugs

**Context:** The original shared memory helpers were `bool` functions using plain `memcpy()`:
```cpp
bool uint8_to_int16_memcpy(const uint8_t* src, size_t src_size, int16_t* dst, size_t dst_len) {
  memcpy(dst, src, required);
  return true;
}
```

These were rewritten to handle `volatile` properly with 32-bit operations and changed to `void` return type. But during manual integration, two bugs slipped in:

1. **`return true;` left in void functions** — the old `bool` return statements were left behind. Compile error.
2. **Wrong function name** — `int16_to_uint8_memcpy()` was called instead of `int16_to_uint8()` after renaming. Link error.

These are trivial bugs but illustrate a real pattern: when manually merging LLM-generated diffs into evolving code, copy-paste artifacts are common. The function signature changed (bool→void, parameter list changed) but the body wasn't fully cleaned up.

**Lesson:** When refactoring function signatures, always re-read the entire function body. LLMs generate clean diffs, but humans applying diffs often leave artifacts.

---

## Dead End 13: Rejected Optimizations (User Filtered Signal from Noise)

Several optimizations were suggested by Claude and explicitly rejected by the user as not worth the complexity:

**Rejected: `hsem_activate_notification_batch()` function**
- Claude suggested combining the two `hsem_activate_notification()` calls in `setup()` into a single register write
- User: "i don't care about hsem_activate_notification_batch, since it runs once. so remove this"
- Correct call — optimizing one-time init code is pointless

**Rejected: 4x loop unrolling for random data generation**
- Claude suggested manually unrolling the random generation loop
- User: "i don't care about 'Generate random int16_t values in 32-bit chunks, 4x unrolled' since it's just test data"
- Correct call — the random data was for testing shared memory, not production audio

**Rejected: DMA for M4 copies** (later became moot)
- Claude implemented full DMA1 setup with burst mode for M4→M7 copies
- User: "i don't need the M4 back to M7 performance. that was solely for tests. so remove the dma related stuff"
- The test harness was optimized but the test itself was about to be replaced

**Lesson:** Not every optimization is worth taking. The user consistently applied the filter: "does this affect the hot path in production?" If no, skip it. This is the right discipline — optimize what matters, not what's easy to optimize.

---

## Evolution: uint8_t Shared Memory → int16_t Typed Access

**Not a dead end, but a significant refactoring step** that went through multiple iterations.

The shared memory regions are `volatile uint8_t*` — raw bytes. But audio data is `int16_t`. The user wanted to work with typed `int16_t` arrays in the M7 write path rather than raw bytes.

This required:
1. Rewriting the conversion functions to be `volatile`-aware (the old ones used plain `memcpy()` which isn't safe with `volatile`)
2. Using 32-bit word operations for performance (each 32-bit read/write handles 2 int16 samples)
3. Handling the odd-count edge case (when sample count isn't divisible by 2)

The final volatile-aware functions became the foundation for all shared memory access:
```cpp
static inline void int16_to_uint8(const int16_t* src, volatile uint8_t* dst, size_t count) {
  const uint32_t* src32 = (const uint32_t*)src;
  volatile uint32_t* dst32 = (volatile uint32_t*)dst;
  for (size_t i = 0; i < count / 2; i++) {
    dst32[i] = src32[i];  // 2 samples per write, volatile-safe
  }
}
```

This went through 3 iterations: `memcpy`-based (broken with volatile) → byte-by-byte volatile (slow) → 32-bit volatile operations (correct + fast).

---

## Dead End 14: Backpressure — Where to Wait for Region Availability

**Four options were analyzed for where to block when both regions are busy:**

- **Option A:** Wait at start of `WAIT_PAYLOAD` — too late, already committed to parsing
- **Option B:** Wait right after flipping region — blocks `parse()`, serial bytes pile up
- **Option C:** Wait on first sample byte of next packet — saves 5 header bytes of buffering time
- **Option D:** Wait immediately after HSEM trigger — cleanest, well-defined blocking point

**Option D was selected** as the simplest and most predictable. The 5 bytes of runway from Option C wasn't worth the added complexity.

But then during implementation, the approach evolved further: the `_acquire_region()` function was added, which **tries both regions before spinning**. This handles the case where region processing finishes out-of-order (e.g., M4 finishes region 1 before region 0). The spin-wait only happens when genuinely both regions are occupied.

---

## Key Lessons (for blog conclusions)

1. **Arduino abstractions have a cost.** `digitalWrite()` is fine for blinking LEDs. For audio-rate signals, you need bare-metal register access.

2. **The STM32H7's D-cache is both a performance feature and the #1 source of dual-core bugs.** Every shared memory write needs a cache clean. Every shared memory read needs a cache invalidate. Forgetting either produces intermittent data corruption that's extremely hard to debug.

3. **Hardware semaphores are notifications, not mailboxes.** They're one-shot (must re-arm), they need a take-then-release transition to fire (can't just release), and they carry zero data — all actual data goes through shared memory.

4. **LLMs were essential for navigating STM32 HAL complexity.** The HAL documentation is vast and often unclear about which functions are mandatory vs. optional. The ChatGPT sessions identified the IRQ handler conflicts, the one-shot nature of HSEM notifications, and the volatile/memcpy incompatibility. The Claude sessions then performed systematic code analysis and generated correct diffs.

5. **Performance optimization is about removing things.** The biggest gains came from eliminating buffer copies, removing abstraction layers, and combining operations — not from adding clever algorithms.

6. **Dead ends are part of the process.** Many of the failed attempts looked correct on paper and only failed when hitting real hardware. Each dead end produced knowledge that no documentation could have provided.
