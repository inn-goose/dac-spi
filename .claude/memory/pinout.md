---
name: Arduino GIGA Pin Mapping
description: Physical pin mapping for DAC SPI signals — PA7 is D5, PB3 (SPI6_SCK) is D91 (not on standard headers)
type: project
---

| Signal | STM32 Pin | Arduino Pin | Physical location |
|--------|-----------|-------------|-------------------|
| SPI6_MOSI (DATA) | PA7 | D5 | Digital header |
| SPI6_SCK (CLOCK) | PB3 | D91 | NOT on standard headers — internal/alt function pin |
| LEFT LATCH | PB8 | D8 | Digital header |
| RIGHT LATCH | PB9 | D9 | Digital header |

**PB3 (SPI6_SCK) location:** On the GIGA's SPI header (6-pin ICSP), middle row, left pin (the SCK position). This is the SPI1 SCK pin (PA5 is SPI1, PB3 is SPI6) — same physical pin PB3 is directly accessible here. Code releases it from JTAG TRACESWO via `DBGMCU->CR &= ~DBGMCU_CR_DBG_TRACECKEN`.

**Note:** PA7 (D5) is the same pin the old bit-banged version used as DATA — intentional, it maps to SPI6_MOSI.

**ICSP/SPI header is SPI1, NOT SPI6:**
- ICSP MISO = PA6, MOSI = PB5, SCK = PA5
