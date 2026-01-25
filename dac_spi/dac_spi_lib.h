#ifndef __dac_spi_lib_h__
#define __dac_spi_lib_h__

namespace DacSpiLibrary {

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


// 16 bit ONLY to /2 the memory consumption
class PcmPlayer : public DacSpiBase {
public:
  PcmPlayer(int dac_resolution, int spi_clock_pin, int spi_data_pin, int spi_latch_pin)
    : DacSpiBase(dac_resolution, spi_clock_pin, spi_data_pin, spi_latch_pin),
      _samples_count(0), _sample_no(0) {}

  void tick() {
    if (_sample_no >= _samples_count) {
      return;
    }
    _sample_no += 1;
  }

  void draw() {
    if (_sample_no >= _samples_count) {
      return;
    }
    int16_t data_16bit = _samples_buffer[_sample_no];
    _send_dac_data((int32_t)data_16bit);
  }

  void play_sample(const int16_t* samples, size_t samples_count) {
    if (samples_count > _BUFFER_SIZE) {
      return;
    }
    memcpy(_samples_buffer, samples, samples_count);
    _samples_count = samples_count;
    _sample_no = 0;
  }

  bool is_playing() {
    return (_sample_no < _samples_count);
  }

  const size_t get_buffer_size() { return _BUFFER_SIZE; }

private:
  static const size_t _BUFFER_SIZE = 8000;
  int16_t _samples_buffer[_BUFFER_SIZE];

  size_t _samples_count;
  size_t _sample_no;
};

}  // DacSpiLibrary

#endif  // !__dac_spi_lib_h__
