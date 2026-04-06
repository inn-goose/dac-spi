---
name: Project State
description: Current architecture and status of DAC SPI project — dual-core streaming with HW SPI, what's done and pending
type: project
---

**Architecture (as of 2026-04-06):**
- M7 core: receives serial data, parses binary protocol v2, writes to shared memory regions
- M4 core: reads shared memory, outputs to DAC via hardware SPI6
- Double-buffered: 2x 32KB regions in D4 SRAM with HSEM signaling
- PcmPlayer reads directly from shared memory (zero-copy)
- Ticker ISR on M4 drives DAC output at configurable sample rate

**What's done:**
- Serial parser: bulk reads, inline int16 assembly, direct shared memory writes
- DAC output: hardware SPI6 replacing bit-banged GPIO, direct GPIO latch via BSRR
- Dual-core IPC: HSEM notifications, cache management, region acquire/release
- Protocol v2: metadata packet + data packets with ACK
- Performance plan in dac_spi/CLAUDE.md with priority-ordered optimization list

**What's pending (from CLAUDE.md):**
- Serial DMA / bulk reads further optimization
- Eliminate remaining buffer copy chain
- ISR flattening (combine L/R operations)

**Goal:** 44.1kHz stereo streaming (CD quality) over USB serial
