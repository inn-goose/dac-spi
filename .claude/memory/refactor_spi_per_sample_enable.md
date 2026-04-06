---
name: Refactor - SPI Enable/Disable Per Sample
description: dac_spi_lib.h enables and disables SPI6 for every single sample — major overhead at high sample rates
type: project
---

**File:** `dac_spi_lib.h:37-62`

**Problem:** The `_send_dac_data()` hot path does this for EVERY sample:
```cpp
HAL_GPIO_WritePin(_latch_port, _latch_pin, GPIO_PIN_SET);   // latch HIGH
MODIFY_REG(spi->CR2, SPI_CR2_TSIZE, 1);                      // set transfer size
SET_BIT(spi->CR1, SPI_CR1_SPE);                               // enable SPI
SET_BIT(spi->CR1, SPI_CR1_CSTART);                            // start transfer
// ... wait for TXP, write data, wait for EOT ...
spi->IFCR = SPI_IFCR_EOTC | SPI_IFCR_TXTFC;                 // clear flags
CLEAR_BIT(spi->CR1, SPI_CR1_SPE);                             // disable SPI
HAL_GPIO_WritePin(_latch_port, _latch_pin, GPIO_PIN_RESET);  // latch LOW
```

At 44.1kHz stereo, this runs 88,200 times per second. The SPI enable/disable + flag clear cycle is significant overhead.

**Potential fix:** Keep SPI enabled between samples. Only toggle the latch:
1. Enable SPI once at playback start
2. Per sample: latch HIGH → write TXDR → wait EOT → clear flags → latch LOW
3. Disable SPI when playback stops

**Challenge:** The latch timing matters for the AD1860. The DAC latches data on the LE (latch enable) falling edge. Need to ensure SPI transfer is complete before dropping latch. Currently this is guaranteed by the EOT wait, which would still be there.

**Bigger challenge:** Two DAC channels share the same SPI6 bus but have different latch pins. The current design sends L then R sequentially. Keeping SPI enabled is fine for this — just toggle latch between channels.

**Risk:** Medium. Need to verify the DAC timing requirements. The AD1860 datasheet specifies minimum LE pulse width.

**Estimated gain:** Eliminates 2 register writes (SPE set/clear) + TSIZE write per sample. At 88.2kHz, this could save ~3-5μs per sample pair.
