---
name: Architecture Challenge - AD1860 SPI vs I2S DAC
description: A modern I2S DAC (PCM5102A) would eliminate SPI bit-banging, ISR ticks, latch GPIO — DMA feeds I2S directly from shared memory
type: project
---

**Challenge:** The AD1860 requires a parallel/SPI-like interface with manual latch control. Every sample needs:
1. Latch HIGH
2. SPI enable, start, write 16 bits, wait EOT, clear flags, disable
3. Latch LOW

This runs in an ISR 44,100× per second (stereo = 88,200×). The entire DacSpiBase, PcmPlayer, Ticker ISR, SPI6 init, latch GPIO mapping exists because of this interface.

**Alternative: I2S DAC (e.g., PCM5102A, ~$2):**
- STM32H747 has I2S hardware peripherals
- I2S + DMA = zero-CPU audio output
- Point DMA source at shared memory region → hardware streams samples automatically
- No ISR needed, no per-sample CPU intervention
- Supports up to 192kHz natively

**What would be eliminated:**
- `dac_spi_lib.h` (entire file)
- `dac_output.h` (most of it — just I2S + DMA init)
- Ticker ISR (DMA handles timing)
- SPI6 init and latch GPIO
- The SPI prescaler bottleneck
- The L/R sequential SPI transfer issue

**What would remain:**
- Serial parsing (unchanged)
- Shared memory (unchanged, DMA reads from it)
- HSEM signaling (unchanged, but simpler — just swap DMA source pointer)

**Why the AD1860 was chosen:** It's a vintage R-2R DAC with a specific sound character. The project is partly about learning to drive it. Also, the blog series is about the AD1860 specifically. Switching DACs would change the project's identity.

**Verdict:** Not a refactoring candidate — the AD1860 is a deliberate hardware choice. But worth understanding that 70% of the firmware complexity exists because of this DAC's interface. A future project with an I2S DAC would be dramatically simpler.
