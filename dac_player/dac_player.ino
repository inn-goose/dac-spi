// This sketch designed to work on Arduino GIGA only
// lower frequency versions are not sustainable to generate the SPI on high frequencies

#include "mbed.h"  // for timer

#include "dac_spi.h"


// wiring
static const int CLOCK_PIN = 6;
static const int DATA_PIN = 5;
static const int LEFT_LATCH_ENABLE_PIN = 8;
static const int RIGHT_LATCH_ENABLE_PIN = 9;

void setup_pins() {
  pinMode(CLOCK_PIN, OUTPUT);
  pinMode(DATA_PIN, OUTPUT);
  pinMode(LEFT_LATCH_ENABLE_PIN, OUTPUT);
  pinMode(RIGHT_LATCH_ENABLE_PIN, OUTPUT);
  digitalWrite(LEFT_LATCH_ENABLE_PIN, LOW);
  digitalWrite(RIGHT_LATCH_ENABLE_PIN, LOW);
}


// external DAC chip resolution
// this code supports 16 and 18 bit DACs only
static const int DAC_RESOLUTION = 16;  // bit

// Giga supports:
// * 8000 / RC = 2kOm + 10nF (103)
// * 11025
// * 16000 / RC = 1kOm + 10nF (103)
// * 22050
// * 32000 / RC = 10kOm + 0nF (103)
// * 44100 / 16 bit / mono (MAX)
static const unsigned long DAC_SAMPLE_RATE = 32000;  // Hz

// internal timer to maintain sample rate

// one digitalWrite for Arduino UNO R3 costs ~5us
// one channel write costs data + clock + latch
// so one 18bit DAC channel costs about 18 + 18 * 2 + 2 = 56 digitalWrite
// two channels give 112 digitalWrite operations, plus 10% func overhead
// 112 * 5us + 10% = 616us
// so the closest frequency for UNO R3 would be 1000, but let's add some reserve
// 800 Hz or 1.25ms per operation for both channels
mbed::Ticker irsTicker;

void setup_sample_rate_timer() {
  irsTicker.attach(&sample_rate_timer_callback, 1.0f / float(DAC_SAMPLE_RATE));

}

volatile bool sample_rate_timer_tick = false;

void sample_rate_timer_callback() {
  sample_rate_timer_tick = true;
}


PcmPlayer pcm_player_L(DAC_RESOLUTION, CLOCK_PIN, DATA_PIN, LEFT_LATCH_ENABLE_PIN);


void setup() {
  setup_pins();
  setup_sample_rate_timer();
}


void loop() {
  if (sample_rate_timer_tick) {
    sample_rate_timer_tick = false;
    // tick
    pcm_player_L.tick();
    // draw
    pcm_player_L.draw();
  }
}
