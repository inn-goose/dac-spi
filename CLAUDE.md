# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

External audio card / DAC music player: PC streams WAV files over USB serial to an Arduino GIGA R1 WiFi (STM32H747), which outputs 16-bit PCM audio to AD1860 DAC chips via hardware SPI. Dual-core: M7 receives serial, M4 outputs to DAC. Can play oscilloscope music.

## Build & Upload

Both cores compile from the same sketch. The `CORE_CM7` / `CORE_CM4` preprocessor defines select the code path.

```bash
# Compile M7 (serial receiver)
arduino-cli compile --fqbn arduino:mbed_giga:giga:target_core=cm7 ./dac_spi/dac_spi.ino

# Compile M4 (DAC output)
arduino-cli compile --fqbn arduino:mbed_giga:giga:target_core=cm4 ./dac_spi/dac_spi.ino

# Upload (same port for both, upload M7 first)
arduino-cli upload -p /dev/cu.usbmodem2101 --fqbn arduino:mbed_giga:giga:target_core=cm7 ./dac_spi/dac_spi.ino
arduino-cli upload -p /dev/cu.usbmodem2101 --fqbn arduino:mbed_giga:giga:target_core=cm4 ./dac_spi/dac_spi.ino
```

## CLI Setup & Usage

```bash
# One-time setup
PATH=${PATH}:~/Library/Python/3.14/bin/ ./env/init.sh
source venv/bin/activate
export PYTHONPATH=./dac_spi_cli/:$PYTHONPATH

# Stream a WAV file (first arg is serial port, -s is the file)
./dac_spi_cli/cli.py /dev/cu.usbmodem2101 -s ./samples/32k_tone_500.wav --debug
```

Dependencies: `pyserial`, `numpy` (see `env/requirements_cli.txt`).

## Architecture

**Dual-core data flow:**
```
PC (cli.py) --USB serial--> M7: StreamingParser --shared memory--> M4: DacOutput --SPI6--> DAC chip
```

**M7 (Cortex-M7, 480MHz):** Receives serial bytes via bulk `Serial.readBytes()`, parses binary protocol v2, writes samples directly to shared memory regions in D4 SRAM, signals M4 via HSEM.

**M4 (Cortex-M4, 240MHz):** Waits for HSEM signal, reads header + samples from shared memory, drives DAC via hardware SPI6 at the sample rate using `mbed::Ticker` ISR. PcmPlayer reads directly from volatile shared memory (zero-copy). Signals M7 when region is free.

**Double-buffering:** Two 32KB regions at `0x38000000` and `0x38008000` (D4 SRAM). Each has a 32-byte cache-aligned header (`SharedRegionHeader`) followed by int16 samples (max 16,368 per region). M7 writes one while M4 reads the other.

**HSEM signaling:** Four hardware semaphores (IDs 28-31). `FastTake + Release` is required — Release alone doesn't fire the interrupt. Notifications are one-shot and must be re-armed in the callback.

**Cache coherency (M7 only):** M7 has D-cache, M4 doesn't. After writing shared memory: `cleanDCache()`. Before reading: `invalidateDCache()`. Uses `__DMB()` barrier (not `__DSB()` or `__ISB()`).

### Key Files

| File | Role |
|------|------|
| `dac_spi/dac_spi.ino` | Main sketch, wiring constants, setup/loop |
| `dac_spi/serial_streaming_lib.h` | RingBuffer + StreamingParser (protocol v2, writes to shared memory) |
| `dac_spi/dac_output.h` | DacOutput class, SPI6 init (MOSI=PA7, SCK=PB3), Ticker ISR playback |
| `dac_spi/dac_spi_lib.h` | DacSpiBase (HW SPI driver) + PcmPlayer (reads from volatile shared memory, stride/offset for stereo) |
| `dac_spi/core_m7.h` | M7 boot, HSEM callbacks (WRITE_HSEM_ID 28-29), setup |
| `dac_spi/core_m4.h` | M4 playback loop, HSEM callbacks (READ_HSEM_ID 30-31) |
| `dac_spi/core_mem.h` | Shared memory layout, SharedRegionHeader struct, cache ops, volatile-aware copy functions |
| `dac_spi/core_hsem.h` | HSEM wrappers (`hsem_init`, `hsem_trigger`) |
| `dac_spi/serial_json_rpc_lib.h` | Deprecated JSON-RPC protocol (unused, kept for reference) |
| `dac_spi_cli/cli.py` | Python CLI: WAV parsing, metadata+data packet streaming, per-packet ACK |

### Serial Protocol v2

Common header: `[0xAB][0xCD][0xEF][TYPE]`
- TYPE=0x01: Metadata (32 bytes: n_channels, bits_per_sample, sample_rate, packet_size, total_samples, debug, reserved)
- TYPE=0x02: Data (payload = packet_size * 2 bytes of LE int16 samples, last packet zero-padded)
- Device ACKs each packet with `"1"`

### Wiring

| Signal | Arduino Pin | Location |
|--------|-------------|----------|
| DATA (SPI6 MOSI) | D5 (PA7) | Digital header |
| CLOCK (SPI6 SCK) | PB3 | SPI header, middle row, left pin |
| LEFT LATCH | D8 (PB8) | Digital header |
| RIGHT LATCH | D9 (PB9) | Digital header |

## Critical Constraints

- `memcpy()` is NOT safe with volatile shared memory — use `uint8_to_int16()` / `int16_to_uint8()` from `core_mem.h`
- Never use `HSEM->RLR[]` for signaling — only `HAL_HSEM_FastTake()` + `HAL_HSEM_Release()` generates interrupts
- Never use `.RAM_D2` linker sections for shared memory — addresses differ per core. Use hardcoded D4 SRAM addresses
- PB3 defaults to JTAG TRACESWO — must release via `DBGMCU->CR &= ~DBGMCU_CR_DBG_TRACECKEN`
- HSEM notifications are one-shot — must re-arm with `HAL_HSEM_ActivateNotification()` inside every callback
- OpenAMP / RPC.h conflicts with manual HSEM handlers — can't coexist, project uses bare-metal IPC

## Performance Status

**Goal:** 44.1kHz+ stereo streaming (176,400 samples/sec).

**Current state:** Can play oscilloscope music. Hardware SPI6, dual-core, zero-copy playback all working.

| Optimization | Status |
|-------------|--------|
| Direct GPIO register access (BSRR) for latch pins | DONE |
| Hardware SPI6 replacing bit-banged GPIO | DONE |
| Serial bulk reads (`Serial.readBytes()`, 256B chunks) | DONE |
| Lighter memory barriers (`__DMB()` instead of `__DSB()+__ISB()`) | DONE |
| Zero-copy PcmPlayer (volatile pointer to shared memory) | DONE |
| Batched HSEM callback (constexpr masks, single clear+reactivate) | DONE |
| Contiguous cache clean (single call for all regions) | DONE |
| Serial baud rate 2000000 | DONE |
| Ticker ISR direct playback (no flag polling) | DONE |
| Protocol v2 (metadata once + pure data stream) | DONE |
| HSEM FastTake removal | NOT VALID — required for interrupt generation |
| HSEM direct register access (RLR) | NOT VALID — doesn't fire interrupts |
| Further ISR flattening (combine L/R) | TODO |
| UART DMA (mbed serial DMA) | TODO |
