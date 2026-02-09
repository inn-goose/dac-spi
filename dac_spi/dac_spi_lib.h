#ifndef __dac_spi_lib_h__
#define __dac_spi_lib_h__

#include "stm32h7xx_hal.h"
#include "core_mem.h"

namespace DacSpiLibrary {

class DacSpiBase {
public:
  DacSpiBase(int dac_resolution,
             GPIO_TypeDef* clock_port, uint16_t clock_pin,
             GPIO_TypeDef* data_port, uint16_t data_pin,
             GPIO_TypeDef* latch_port, uint16_t latch_pin) {
    // support 16 and 18 bit only
    if (dac_resolution != 16 && dac_resolution != 18) {
      return;
    }

    _dac_resolution = dac_resolution;
    _dac_max_val = pow(2.0, float(_dac_resolution - 1)) - 1;
    _dac_min_val = -pow(2.0, float(_dac_resolution - 1));

    _clock_port = clock_port;
    _clock_pin = clock_pin;
    _data_port = data_port;
    _data_pin = data_pin;
    _latch_port = latch_port;
    _latch_pin = latch_pin;
  }

  void tick() {}
  void draw() {}

protected:
  int _dac_resolution;
  int32_t _dac_max_val;
  int32_t _dac_min_val;

  GPIO_TypeDef* _clock_port;
  GPIO_TypeDef* _data_port;
  GPIO_TypeDef* _latch_port;
  uint16_t _clock_pin;
  uint16_t _data_pin;
  uint16_t _latch_pin;

  void _send_dac_data(int32_t data) {
    HAL_GPIO_WritePin(_latch_port, _latch_pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(_clock_port, _clock_pin, GPIO_PIN_RESET);

    for (int i = _dac_resolution - 1; i >= 0; i--) {
      HAL_GPIO_WritePin(_data_port, _data_pin,
                        ((data >> i) & 1) ? GPIO_PIN_SET : GPIO_PIN_RESET);
      HAL_GPIO_WritePin(_clock_port, _clock_pin, GPIO_PIN_SET);
      HAL_GPIO_WritePin(_clock_port, _clock_pin, GPIO_PIN_RESET);
    }

    HAL_GPIO_WritePin(_latch_port, _latch_pin, GPIO_PIN_RESET);
  }
};


// 16 bit ONLY to /2 the memory consumption
class PcmPlayer : public DacSpiBase {
public:
  static const size_t BUFFER_SIZE = CoreMemory::MAX_SAMPLES_PER_REGION;

  PcmPlayer(int dac_resolution,
            GPIO_TypeDef* clock_port, uint16_t clock_pin,
            GPIO_TypeDef* data_port, uint16_t data_pin,
            GPIO_TypeDef* latch_port, uint16_t latch_pin)
    : DacSpiBase(dac_resolution, clock_port, clock_pin, data_port, data_pin, latch_port, latch_pin),
      _samples_count(0), _sample_no(0) {}

  void tick() {
    // Increment after draw() has used current sample
    if (_sample_no < _samples_count) {
      _sample_no += 1;
    }
  }

  void draw() {
    if (_sample_no >= _samples_count) {
      return;
    }
    int16_t data_16bit = _samples_buffer[_sample_no];
    _send_dac_data((int32_t)data_16bit);
  }

  void play_sample(const int16_t* samples, size_t samples_count) {
    if (samples_count > BUFFER_SIZE) {
      return;
    }
    memcpy(_samples_buffer, samples, samples_count * sizeof(int16_t));
    _samples_count = samples_count;
    _sample_no = 0;
  }

  bool is_playing() {
    return (_sample_no < _samples_count);
  }

private:
  int16_t _samples_buffer[BUFFER_SIZE];

  size_t _samples_count;
  size_t _sample_no;
};

}  // DacSpiLibrary

#endif  // !__dac_spi_lib_h__
