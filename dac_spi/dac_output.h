#ifndef __dac_output_h__
#define __dac_output_h__

#include <mbed.h>
#include "stm32h7xx_hal.h"
#include "dac_spi_lib.h"

using namespace DacSpiLibrary;

// Global pointer for ISR callback
class DacOutput;
static DacOutput* g_dac_instance = nullptr;

// STM32 GPIO mappings for Arduino GIGA latch pins
struct GpioPin {
  GPIO_TypeDef* port;
  uint16_t pin;
};

static GpioPin arduinoToGpio(int arduinoPin) {
  switch (arduinoPin) {
    case 8:  return {GPIOB, GPIO_PIN_8};   // LEFT_LATCH
    case 9:  return {GPIOB, GPIO_PIN_9};   // RIGHT_LATCH
    default: return {GPIOA, GPIO_PIN_0};   // fallback
  }
}

// SPI6 (D3 domain / APB4 — always-on, works on M4)
// MOSI: PA7 (AF8), SCK: PB3 (AF8)
static SPI_HandleTypeDef hspi6;

static void spi6_init() {
  __HAL_RCC_SPI6_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  // Release PB3 from JTAG TRACESWO — PB3 defaults to SWO after reset
  DBGMCU->CR &= ~DBGMCU_CR_DBG_TRACECKEN;

  // Configure PA7 as SPI6_MOSI (AF8)
  GPIO_InitTypeDef gpio = {};
  gpio.Pin = GPIO_PIN_7;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  gpio.Alternate = GPIO_AF8_SPI6;
  HAL_GPIO_Init(GPIOA, &gpio);

  // Configure PB3 as SPI6_SCK (AF8)
  gpio.Pin = GPIO_PIN_3;
  gpio.Alternate = GPIO_AF8_SPI6;
  HAL_GPIO_Init(GPIOB, &gpio);

  // Configure SPI6: Mode 0, MSB first, 16-bit, TX-only
  hspi6.Instance = SPI6;
  hspi6.Init.Mode = SPI_MODE_MASTER;
  hspi6.Init.Direction = SPI_DIRECTION_2LINES_TXONLY;
  hspi6.Init.DataSize = SPI_DATASIZE_16BIT;
  hspi6.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi6.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi6.Init.NSS = SPI_NSS_SOFT;
  hspi6.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_16;
  hspi6.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi6.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi6.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi6.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
  hspi6.Init.MasterKeepIOState = SPI_MASTER_KEEP_IO_STATE_ENABLE;
  HAL_SPI_Init(&hspi6);
}

class DacOutput {
public:
  DacOutput(int left_latch_pin, int right_latch_pin, int resolution)
    : _left_latch_pin(left_latch_pin),
      _right_latch_pin(right_latch_pin),
      _resolution(resolution),
      _pcm_player_L(nullptr),
      _pcm_player_R(nullptr),
      _playing(false),
      _ticker_running(false),
      _completed_region(-1) {
  }

  void setup() {
    // Initialize SPI6 hardware (MOSI=PA7, SCK=PB3)
    spi6_init();

    // Configure latch pins as GPIO output
    pinMode(_left_latch_pin, OUTPUT);
    pinMode(_right_latch_pin, OUTPUT);
    digitalWrite(_left_latch_pin, LOW);
    digitalWrite(_right_latch_pin, LOW);

    // Create players with SPI6 handle + latch GPIO
    GpioPin left_latch = arduinoToGpio(_left_latch_pin);
    GpioPin right_latch = arduinoToGpio(_right_latch_pin);

    _pcm_player_L = new PcmPlayer(_resolution,
                                  &hspi6,
                                  left_latch.port, left_latch.pin);

    _pcm_player_R = new PcmPlayer(_resolution,
                                  &hspi6,
                                  right_latch.port, right_latch.pin);

    g_dac_instance = this;
  }

  // Non-blocking: starts playback, returns immediately
  bool start_playback(uint32_t n_channels, uint32_t sample_rate, uint32_t samples_count, volatile int16_t* samples, int region_id) {
    if (_playing) return false;

    _current_region = region_id;

    if (n_channels == 1) {
      _pcm_player_L->play_from_volatile(samples, samples_count);
    } else if (n_channels == 2) {
      uint32_t frames = samples_count / 2;
      _pcm_player_L->play_from_interleaved(samples, frames, 0, 2);
      _pcm_player_R->play_from_interleaved(samples, frames, 1, 2);
    }

    _playing = true;

    // Only attach Ticker once — keep it running between buffers
    if (!_ticker_running) {
      unsigned long sample_period_us = 1000000UL / sample_rate;
      _ticker.attach_us(mbed::callback(&DacOutput::_isr_static), sample_period_us);
      _ticker_running = true;
    }
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
    if (!_playing) return;  // idle tick — Ticker stays running

    _pcm_player_L->draw_and_tick();
    _pcm_player_R->draw_and_tick();

    if (!_pcm_player_L->is_playing() && !_pcm_player_R->is_playing()) {
      _playing = false;
      _completed_region = _current_region;
      // DON'T detach — Ticker keeps running, ISR returns early until next buffer
    }
  }

  // Pins
  int _left_latch_pin, _right_latch_pin;

  // Config
  int _resolution;

  // Players
  PcmPlayer* _pcm_player_L;
  PcmPlayer* _pcm_player_R;

  // Ticker
  mbed::Ticker _ticker;

  // State
  volatile bool _playing;
  bool _ticker_running;
  volatile int _current_region;
  volatile int _completed_region;

};

#endif  // !__dac_output_h__
