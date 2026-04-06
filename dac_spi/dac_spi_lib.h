#ifndef __dac_spi_lib_h__
#define __dac_spi_lib_h__

#include "stm32h7xx_hal.h"
#include "core_mem.h"

namespace DacSpiLibrary {

class DacSpiBase {
public:
  DacSpiBase(int dac_resolution,
             SPI_HandleTypeDef* hspi,
             GPIO_TypeDef* latch_port, uint16_t latch_pin) {
    // support 16 and 18 bit only
    if (dac_resolution != 16 && dac_resolution != 18) {
      return;
    }

    _dac_resolution = dac_resolution;
    _dac_max_val = pow(2.0, float(_dac_resolution - 1)) - 1;
    _dac_min_val = -pow(2.0, float(_dac_resolution - 1));

    _hspi = hspi;
    _latch_port = latch_port;
    _latch_pin = latch_pin;
  }

protected:
  int _dac_resolution;
  int32_t _dac_max_val;
  int32_t _dac_min_val;

  SPI_HandleTypeDef* _hspi;
  GPIO_TypeDef* _latch_port;
  uint16_t _latch_pin;

  inline void _send_dac_data(int32_t data) {
    SPI_TypeDef* spi = _hspi->Instance;

    HAL_GPIO_WritePin(_latch_port, _latch_pin, GPIO_PIN_SET);

    // Set transfer size = 1 frame
    MODIFY_REG(spi->CR2, SPI_CR2_TSIZE, 1);
    // Enable SPI + start transfer
    SET_BIT(spi->CR1, SPI_CR1_SPE);
    SET_BIT(spi->CR1, SPI_CR1_CSTART);

    // Wait for TX FIFO ready, then write data
    volatile uint32_t timeout = 1000;
    while (!(spi->SR & SPI_FLAG_TXP) && --timeout) {}
    *(volatile uint16_t*)&spi->TXDR = (uint16_t)data;

    // Wait for end of transfer
    timeout = 1000;
    while (!(spi->SR & SPI_FLAG_EOT) && --timeout) {}

    // Clear flags + disable SPI
    spi->IFCR = SPI_IFCR_EOTC | SPI_IFCR_TXTFC;
    CLEAR_BIT(spi->CR1, SPI_CR1_SPE);

    HAL_GPIO_WritePin(_latch_port, _latch_pin, GPIO_PIN_RESET);
  }
};


// 16 bit ONLY to /2 the memory consumption
class PcmPlayer : public DacSpiBase {
public:
  PcmPlayer(int dac_resolution,
            SPI_HandleTypeDef* hspi,
            GPIO_TypeDef* latch_port, uint16_t latch_pin)
    : DacSpiBase(dac_resolution, hspi, latch_port, latch_pin),
      _samples(nullptr), _samples_count(0), _sample_no(0),
      _stride(1), _offset(0) {}

  inline void draw_and_tick() {
    if (_sample_no >= _samples_count) return;
    _send_dac_data((int32_t)_samples[_sample_no * _stride + _offset]);
    _sample_no++;
  }

  // Point directly at shared memory — no copy
  void play_from_volatile(volatile int16_t* src, size_t count) {
    _samples = src;
    _samples_count = count;
    _stride = 1;
    _offset = 0;
    _sample_no = 0;
  }

  void play_from_interleaved(volatile int16_t* src, size_t frames, int channel, int n_channels) {
    _samples = src;
    _samples_count = frames;
    _stride = n_channels;
    _offset = channel;
    _sample_no = 0;
  }

  bool is_playing() {
    return (_sample_no < _samples_count);
  }

private:
  volatile int16_t* _samples;  // points directly into shared memory
  size_t _samples_count;
  size_t _sample_no;
  int _stride;   // 1 for mono, 2 for stereo interleaved
  int _offset;   // 0 for L channel, 1 for R channel
};

}  // DacSpiLibrary

#endif  // !__dac_spi_lib_h__
