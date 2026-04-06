---
name: Refactor - SPI6 Shared Between L/R Channels is a Bottleneck
description: Both DAC channels use the same SPI6 bus sequentially — at high sample rates this doubles the time per sample
type: project
---

**File:** `dac_output.h:148-153` (ISR calls L then R)

**Problem:** The ISR does:
```cpp
_pcm_player_L->draw_and_tick();
_pcm_player_R->draw_and_tick();
```

Each `draw_and_tick()` calls `_send_dac_data()` which does a full SPI transaction. For stereo, that's two sequential SPI transfers per sample period. At 44.1kHz, each period is 22.67μs. Two SPI transactions must fit in that window.

With `SPI_BAUDRATEPRESCALER_16` on a ~100MHz APB4 clock, SPI6 runs at ~6.25MHz. A 16-bit transfer takes 2.56μs plus latch timing overhead. Two transfers = ~6-8μs. This fits in 22.67μs, but leaves less headroom than ideal.

**Potential fixes:**
1. **Lower prescaler:** `SPI_BAUDRATEPRESCALER_8` or `_4` would double/quadruple SPI clock speed. Need to check AD1860 max clock rate.
2. **Separate SPI for each channel:** STM32H747 has multiple SPI peripherals. But SPI6 is the only one in D3 domain (accessible from M4). SPI1-5 are in D1/D2.
3. **DMA-driven SPI:** Use DMA to feed SPI6, freeing the CPU. The ISR would just start the DMA transfer. But sequencing two channels with different latch pins via DMA is complex.
4. **Interleaved latch timing:** Send L data, don't wait for EOT, immediately send R data, latch L when R starts... this is getting into timing-critical territory.

**Risk:** Changing SPI prescaler is easy but needs hardware verification. DMA approach is complex.

**Estimated gain:** Lower prescaler could cut SPI time by 2-4x, giving more ISR headroom.
