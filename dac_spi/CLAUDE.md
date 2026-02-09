# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Arduino GIGA R1 firmware for driving external DAC chips via software SPI. Streams 16-bit PCM audio over serial and outputs to one or two DAC channels. Designed specifically for the STM32H747 dual-core processor (Cortex-M7 + Cortex-M4).

## Build & Upload

This is an Arduino sketch. Use Arduino IDE or arduino-cli:

```bash
# Compile for Arduino GIGA R1 WiFi
arduino-cli compile --fqbn arduino:mbed_giga:giga dac_spi.ino

# Upload
arduino-cli upload -p /dev/cu.usbmodemXXXX --fqbn arduino:mbed_giga:giga dac_spi.ino
```

## Architecture

### Core Files

- **dac_spi.ino** - Main sketch: pin setup, sample rate timer (mbed::Ticker), streaming loop
- **dac_spi_lib.h** - `DacSpiBase` (bit-banged SPI) and `PcmPlayer` (sample buffer playback)
- **serial_streaming_lib.h** - `RingBuffer` and `StreamingParser` for binary packet protocol

### Dual-Core Support (currently unused in main loop)

- **core_m7.h** - M7 core: boots M4, writes to shared memory, triggers HSEM
- **core_m4.h** - M4 core: waits for HSEM signals, reads shared memory
- **core_hsem.h** - Hardware semaphore wrappers for inter-core sync
- **core_mem.h** - Shared memory regions at 0x38000000 (D4 SRAM), cache management

### Serial Streaming Protocol (v2)

All packets share a 4-byte header: `[0xAB][0xCD][0xEF][TYPE]`

Metadata packet (TYPE=0x01, sent once before streaming, 32 bytes after type):
```
Offset  Size  Field
0       1     n_channels: u8
1       1     bits_per_sample: u8
2       4     sample_rate: u32 LE
6       2     packet_size: u16 LE
8       4     total_samples: u32 LE
12      1     debug: u8
13      19    reserved (zeros)
```

Data packet (TYPE=0x02, sent repeatedly):
`[0xAB][0xCD][0xEF][0x02][payload: packet_size × 2 bytes]`
- Last packet is zero-padded to packet_size

Device acknowledges each packet (metadata and data) by sending `1`.

### Key Constants

- DAC_RESOLUTION: 16 or 18 bit (currently 16)
- DAC_SAMPLE_RATE: 8000 Hz default, max 44100 Hz mono
- Pin assignments: CLOCK=6, DATA=5, LEFT_LATCH=8, RIGHT_LATCH=9

### Conditional Compilation

Use `CORE_CM7` / `CORE_CM4` defines to target specific cores. The Arduino GIGA can run separate firmware on each core.

---

# Performance Optimization Plan

**Goal:** Achieve 44.1kHz+ stereo streaming (176,400 samples/sec) with minimal CPU overhead.

**Current State:** 8kHz with ~50% idle gaps between packets due to CPU bottlenecks.

---

## Tier 1: Critical Path (MUST HAVE)

### 1.1 Direct GPIO Register Access (Quick Win) — DONE

**Problem:** digitalWrite() has overhead (pin validation, etc.)

**Solution:** Direct register writes for latch pins

**Implementation:**
```cpp
// Get GPIO port and pin mask once in setup()
GPIO_TypeDef* latch_port = digitalPinToPort(_spi_latch_pin);
uint16_t latch_mask = digitalPinToBitMask(_spi_latch_pin);

// In hot path — single cycle writes
latch_port->BSRR = latch_mask;           // Set HIGH
latch_port->BSRR = latch_mask << 16;     // Set LOW
```

**Files to modify:**
- `dac_spi_lib.h`: Cache port/mask in constructor, use BSRR

**Status:** Implemented. `dac_output.h` maps Arduino pins via `arduinoToGpio()`, `dac_spi_lib.h` uses `HAL_GPIO_WritePin()` with cached port/pin.

**Estimated gain:** 10-20x faster GPIO (200 cycles → 1 cycle)
**Difficulty:** Easy

---

## Tier 2: High Impact (SHOULD HAVE)

### 2.1 Serial DMA / Bulk Reads

**Problem:** `serial_streaming_lib.h:85-87` reads one byte at a time

**Solution:** Use `Serial.readBytes()` for bulk reads

**Implementation:**
```cpp
void loop() {
  int avail = Serial.available();
  if (avail > 0) {
    uint8_t bulk_buffer[256];
    int count = min(avail, 256);
    Serial.readBytes(bulk_buffer, count);
    for (int i = 0; i < count; i++) {
      _ring_buffer.push(bulk_buffer[i]);
    }
  }
  parse();
}
```

**Better:** Arduino GIGA supports UART DMA via mbed
```cpp
// Direct DMA to ring buffer (advanced)
serial.read(ring_buffer_ptr, max_size, callback, SERIAL_EVENT_RX_ALL);
```

**Files to modify:**
- `serial_streaming_lib.h`: Replace byte-by-byte with bulk read

**Estimated gain:** 5-10x fewer function calls
**Difficulty:** Easy (bulk) / Medium (DMA)

---

### 2.2 Eliminate Buffer Copy Chain

**Current flow (3 copies!):**
```
Serial → RingBuffer → SharedMem → DacOutput._left_buffer → PcmPlayer._samples_buffer
```

**Target flow (1 copy):**
```
Serial → RingBuffer → SharedMem → [DMA reads directly]
```

**Implementation:**
- Remove `_left_buffer` / `_right_buffer` from DacOutput
- Have PcmPlayer read directly from shared memory pointer
- Or: Use DMA source address pointing to shared memory

**Challenge:** Stereo de-interleaving still needed
- Option A: De-interleave in M7 before writing to shared mem (2 regions per channel)
- Option B: DMA scatter-gather (complex, may not be available)
- Option C: Accept one copy for de-interleave, eliminate the rest

**Files to modify:**
- `dac_output.h`: Remove intermediate buffers
- `dac_spi_lib.h`: PcmPlayer accepts volatile pointer directly
- `core_mem.h`: Consider separate L/R regions

**Estimated gain:** 32KB less memory movement per packet
**Difficulty:** Medium

---

### 2.3 Optimize ISR (If Not Using DMA)

**Current ISR in `dac_output.h:120-132`:**
```cpp
void ticker_isr() {
  _pcm_player_L->tick();
  _pcm_player_R->tick();
  _pcm_player_L->draw();
  _pcm_player_R->draw();
  // ... completion check
}
```

**Optimizations:**
1. Inline everything — avoid virtual calls
2. Combine L/R into single operation
3. Use direct register access (see 1.3)
4. Remove redundant `is_playing()` checks

**Files to modify:**
- `dac_output.h`: Flatten ISR
- `dac_spi_lib.h`: Make methods inline

**Estimated gain:** 2-3x faster ISR
**Difficulty:** Easy

---

## Tier 3: Polish (NICE TO HAVE)

### 3.1 Lighter Memory Barriers

**Current:** `__DSB()` (full data synchronization barrier)
**Better:** `__DMB()` (data memory barrier — sufficient for HSEM)

**File:** `core_mem.h:99`
```cpp
// Change from:
__DSB();
// To:
__DMB();
```

**Estimated gain:** ~10 cycles per cache operation
**Difficulty:** Easy

---

### 3.2 ~~Remove HSEM FastTake~~ — NOT VALID

**Finding:** FastTake is REQUIRED. The HSEM interrupt notification only fires on the Take→Release transition. Calling Release alone on an already-free semaphore does not trigger the interrupt. System hangs without FastTake.

**Status:** Keep current implementation. Not an optimization opportunity.

---

### 3.3 Increase Serial Baud Rate

**Current:** Likely 460800 or 921600 baud
**Max USB CDC:** 12 Mbps (USB Full Speed) ≈ 1.5 MB/s

For 44.1kHz stereo 16-bit: 176,400 bytes/sec + overhead ≈ 200 KB/s
**Baud rate is NOT the bottleneck** at these rates.

**But:** Ensure `Serial.begin()` uses highest rate:
```cpp
Serial.begin(2000000);  // 2 Mbaud
```

---

### 3.4 Prefetch Next Region

**Idea:** While M4 plays region 0, prefetch region 1 header

```cpp
// In loop_m4(), speculatively read next header
if (dac_output.is_busy() && read_trigger_next) {
  // Prefetch header into local vars (cache warm-up)
  volatile auto* next_header = get_region_header(next_region_id);
  prefetch_n_channels = next_header->n_channels;
  prefetch_samples_count = next_header->samples_count;
}
```

**Estimated gain:** Marginal (D4 SRAM is fast)
**Difficulty:** Easy

---

## Architecture Note: Dual-Core

Dual-core provides:
- Clean separation of concerns
- Guaranteed real-time on M4 (no serial jitter)
- Future expansion (effects processing on M7?)

---

## Implementation Priority Order

| Priority | Task | Gain | Effort | Files | Status |
|----------|------|------|--------|-------|--------|
| ~~1~~ | ~~Direct GPIO registers~~ | 10-20x GPIO | Easy | dac_spi_lib.h | DONE |
| 2 | Serial bulk read | 5-10x serial | Easy | serial_streaming_lib.h | TODO |
| ~~3~~ | ~~Lighter barriers~~ | Minor | Easy | core_mem.h | DONE |
| 4 | Eliminate buffer copies | -32KB/packet | Medium | dac_output.h, dac_spi_lib.h | TODO |

---

## Quick Wins Checklist (< 1 hour each)

- [x] Replace `__DSB()` with `__DMB()` in core_mem.h
- [x] ~~Remove `HAL_HSEM_FastTake()`~~ — NOT VALID (required for interrupt signaling)
- [ ] Add `Serial.readBytes()` bulk read in serial_streaming_lib.h
- [x] Cache GPIO port/mask for latch pins in dac_spi_lib.h
- [x] Increase serial baud rate to 2000000 (cli.py default)

---

## Testing & Verification

1. **Baseline measurement:** Record current packet timing output
2. **After each change:** Compare `total_us`, `parse_us`, `wait_us`
3. **Target metrics:**
   - `wait_us` → 0 (no idle gaps)
   - `total_us` → matches theoretical sample time
4. **Audio quality:** Listen for glitches, measure with scope if available
