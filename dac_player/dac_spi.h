#include "pcm_samples.h"


class DacSpiBase {
public:
  DacSpiBase(int dac_resolution, int spi_clock_pin, int spi_data_pin, int spi_latch_pin) {
    // support 16 and 18 bit only
    if (dac_resolution != 16 && dac_resolution != 18) {
      return;
    }

    _dac_resolution = dac_resolution;
    _dac_max_val = pow(2.0, float(_dac_resolution - 1)) - 1;
    _dac_min_val = -pow(2.0, float(_dac_resolution - 1));

    _spi_clock_pin = spi_clock_pin;
    _spi_data_pin = spi_data_pin;
    _spi_latch_pin = spi_latch_pin;
  }

  void tick() {}
  void draw() {}

protected:
  int _dac_resolution;
  int32_t _dac_max_val;
  int32_t _dac_min_val;

  int _spi_clock_pin;
  int _spi_data_pin;
  int _spi_latch_pin;

  void _send_dac_data(int32_t data) {
    digitalWrite(_spi_latch_pin, HIGH);
    digitalWrite(_spi_clock_pin, LOW);  // clocked by rising edges

    for (int i = _dac_resolution - 1; i >= 0; i--) {
      digitalWrite(_spi_data_pin, (data >> (i)) & 1);
      digitalWrite(_spi_clock_pin, HIGH);  // rising edge clocks data
      digitalWrite(_spi_clock_pin, LOW);
    }

    digitalWrite(_spi_latch_pin, LOW);
  }
};


class PcmPlayer : public DacSpiBase {
public:
  PcmPlayer(int dac_resolution, int spi_clock_pin, int spi_data_pin, int spi_latch_pin)
    : DacSpiBase(dac_resolution, spi_clock_pin, spi_data_pin, spi_latch_pin), _sample_no(0), _samples_total(PCM_SAMPLES_COUNT) {}

  void tick() {
    _sample_no += 1;
    if (_sample_no >= _samples_total) {
      _sample_no = 0;
    }
  }

  void draw() {

    int32_t data_16bit = PCM_SAMPLES_LEFT[_sample_no];
    _send_dac_data(data_16bit);
  }

private:
  unsigned long long _sample_no;
  unsigned long long _samples_total;
};
