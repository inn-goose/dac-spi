#ifndef __dac_output_h__
#define __dac_output_h__

#include <mbed.h>
#include "stm32h7xx_hal.h"
#include "dac_spi_lib.h"

using namespace DacSpiLibrary;

// Global pointer for ISR callback
class DacOutput;
static DacOutput* g_dac_instance = nullptr;

// STM32 GPIO mappings for Arduino GIGA pins
struct GpioPin {
  GPIO_TypeDef* port;
  uint16_t pin;
};

static GpioPin arduinoToGpio(int arduinoPin) {
  switch (arduinoPin) {
    case 5:  return {GPIOA, GPIO_PIN_7};   // DATA
    case 6:  return {GPIOD, GPIO_PIN_13};  // CLOCK
    case 8:  return {GPIOB, GPIO_PIN_8};   // LEFT_LATCH
    case 9:  return {GPIOB, GPIO_PIN_9};   // RIGHT_LATCH
    default: return {GPIOA, GPIO_PIN_0};   // fallback
  }
}

class DacOutput {
public:
  DacOutput(int clock_pin, int data_pin, int left_latch_pin, int right_latch_pin,
            int resolution, unsigned long sample_rate)
    : _clock_pin(clock_pin),
      _data_pin(data_pin),
      _left_latch_pin(left_latch_pin),
      _right_latch_pin(right_latch_pin),
      _resolution(resolution),
      _sample_rate(sample_rate),
      _sample_period_us(1000000UL / sample_rate),
      _pcm_player_L(nullptr),
      _pcm_player_R(nullptr),
      _playing(false),
      _completed_region(-1) {
  }

  void setup() {
    // Still use Arduino pinMode for initial setup
    pinMode(_clock_pin, OUTPUT);
    pinMode(_data_pin, OUTPUT);
    pinMode(_left_latch_pin, OUTPUT);
    pinMode(_right_latch_pin, OUTPUT);
    digitalWrite(_left_latch_pin, LOW);
    digitalWrite(_right_latch_pin, LOW);

    // Create players with STM32 HAL GPIO (faster than digitalWrite)
    GpioPin clock = arduinoToGpio(_clock_pin);
    GpioPin data = arduinoToGpio(_data_pin);
    GpioPin left_latch = arduinoToGpio(_left_latch_pin);
    GpioPin right_latch = arduinoToGpio(_right_latch_pin);

    _pcm_player_L = new PcmPlayer(_resolution,
                                  clock.port, clock.pin,
                                  data.port, data.pin,
                                  left_latch.port, left_latch.pin);

    _pcm_player_R = new PcmPlayer(_resolution,
                                  clock.port, clock.pin,
                                  data.port, data.pin,
                                  right_latch.port, right_latch.pin);

    g_dac_instance = this;
  }

  // Non-blocking: starts playback, returns immediately
  bool start_playback(uint32_t n_channels, uint32_t samples_count, volatile int16_t* samples, int region_id) {
    if (_playing) return false;

    _current_region = region_id;

    if (n_channels == 1) {
      uint32_t count = (samples_count > MAX_SAMPLES) ? MAX_SAMPLES : samples_count;
      for (uint32_t i = 0; i < count; i++) {
        _buf_L[i] = samples[i];
      }
      _pcm_player_L->play_sample(_buf_L, count);

    } else if (n_channels == 2) {
      uint32_t frames = samples_count / 2;
      uint32_t count = (frames > MAX_SAMPLES) ? MAX_SAMPLES : frames;
      for (uint32_t i = 0; i < count; i++) {
        _buf_L[i] = samples[i * 2];
        _buf_R[i] = samples[i * 2 + 1];
      }
      _pcm_player_L->play_sample(_buf_L, count);
      _pcm_player_R->play_sample(_buf_R, count);
    }

    _playing = true;
    _ticker.attach_us(mbed::callback(&DacOutput::_isr_static), _sample_period_us);
    return true;
  }

  bool is_busy() const { return _playing; }

  // Returns region ID that completed, or -1 if none
  int get_completed_region() {
    int r = _completed_region;
    _completed_region = -1;
    return r;
  }

private:
  static void _isr_static() {
    if (g_dac_instance) g_dac_instance->_isr();
  }

  void _isr() {
    // Draw first (output current sample), then tick (advance to next)
    _pcm_player_L->draw();
    _pcm_player_R->draw();
    _pcm_player_L->tick();
    _pcm_player_R->tick();

    if (!_pcm_player_L->is_playing() && !_pcm_player_R->is_playing()) {
      _ticker.detach();
      _playing = false;
      _completed_region = _current_region;
    }
  }

  // Pins
  int _clock_pin, _data_pin, _left_latch_pin, _right_latch_pin;

  // Config
  int _resolution;
  unsigned long _sample_rate;
  unsigned long _sample_period_us;

  // Players
  PcmPlayer* _pcm_player_L;
  PcmPlayer* _pcm_player_R;

  // Ticker
  mbed::Ticker _ticker;

  // State
  volatile bool _playing;
  volatile int _current_region;
  volatile int _completed_region;

  // Static buffers (no VLA)
  static const size_t MAX_SAMPLES = 8000;
  int16_t _buf_L[MAX_SAMPLES];
  int16_t _buf_R[MAX_SAMPLES];
};

#endif  // !__dac_output_h__
