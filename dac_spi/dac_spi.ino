// This sketch designed to work on Arduino GIGA only
// lower frequency versions are not sustainable to generate the SPI on high frequencies

#include "mbed.h"  // for timer

#include "serial_json_rpc_lib.h"
#include "serial_streaming_lib.h"
#include "dac_spi_lib.h"

using namespace SerialJsonRpcLibrary;
using namespace SerialStreamingLibrary;
using namespace DacSpiLibrary;


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
static const unsigned long DAC_SAMPLE_RATE = 8000;  // Hz

// internal timer to maintain sample rate

// one digitalWrite for Arduino UNO R3 costs ~5us
// one channel write costs data + clock + latch
// so one 16bit DAC channel costs about 16 + 16 * 2 + 2 = 50 digitalWrite
// two channels give 100 digitalWrite operations, plus 10% func overhead
// 100 * 5us + 10% = 550us
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


// PCM Player

PcmPlayer pcm_player_L(DAC_RESOLUTION, CLOCK_PIN, DATA_PIN, LEFT_LATCH_ENABLE_PIN);
PcmPlayer pcm_player_R(DAC_RESOLUTION, CLOCK_PIN, DATA_PIN, RIGHT_LATCH_ENABLE_PIN);


// Serial JSON RPC Processor

static SerialJsonRpcBoard rpc_board(rpc_processor);

void rpc_processor(int request_id, const String& method, const String params[], int params_size) {
  if (method == "play_frame") {
    if (params_size != 2) {
      rpc_board.send_error(request_id, JsonRpcErrorCode::INVALID_PARAMS, "Invalid params", "expected: (left_pcm_samples, right_pcm_samples)");
      return;
    }

    const size_t c_buffer_size = pcm_player_L.get_buffer_size();
    int16_t samples_buffer[c_buffer_size];
    size_t left_pcm_samples_count = SerialJsonRpcBoard::json_array_to_i16_array(params[0], samples_buffer, c_buffer_size);
    pcm_player_L.play_sample(samples_buffer, left_pcm_samples_count);
    size_t right_pcm_samples_count = SerialJsonRpcBoard::json_array_to_i16_array(params[1], samples_buffer, c_buffer_size);
    pcm_player_R.play_sample(samples_buffer, right_pcm_samples_count);

    const size_t result_buf_size = 50;
    char result_buf[result_buf_size];
    snprintf(result_buf, result_buf_size, "frame queued: L %d / R %d", left_pcm_samples_count, right_pcm_samples_count);
    rpc_board.send_result_string(request_id, result_buf);

  } else {
    rpc_board.send_error(request_id, JsonRpcErrorCode::METHOD_NOT_FOUND, "Method not found", method.c_str());
  }
}




// Streaming

StreamingParser streaming_parser(streaming_processor);

void streaming_processor(uint8_t n_channels, uint16_t samples_count, int16_t* samples_buffer) {
  // Serial.print("n_channels: ");
  // Serial.print(n_channels);
  // Serial.print(" samples_count: ");
  // Serial.print(samples_count);

  if (n_channels == 1) {
    pcm_player_L.play_sample(samples_buffer, samples_count);

    Serial.print(1);

  } else if (n_channels == 2) {
    const uint16_t channel_samples_count = uint16_t(samples_count / n_channels);
    int16_t left_samples_buffer[channel_samples_count];
    int16_t right_samples_buffer[channel_samples_count];

    for (uint16_t i = 0; i < channel_samples_count; i++) {
      left_samples_buffer[i] = samples_buffer[i * n_channels + 0];
      right_samples_buffer[i] = samples_buffer[i * n_channels + 1];
    }

    pcm_player_L.play_sample(left_samples_buffer, channel_samples_count);
    pcm_player_R.play_sample(right_samples_buffer, channel_samples_count);

    Serial.print(1);
  }
}


// Arduino

void setup() {
  setup_pins();
  setup_sample_rate_timer();

  // rpc board
  rpc_board.init();

  // settings
  long settings[] = {
    (long)DAC_RESOLUTION,
    (long)DAC_SAMPLE_RATE,
  };
  rpc_board.send_result_longs(0, settings, sizeof(settings) / sizeof(settings[0]));
}


void loop() {
  // player
  if (pcm_player_L.is_playing() || pcm_player_R.is_playing()) {
    if (sample_rate_timer_tick) {
      sample_rate_timer_tick = false;
      // tick
      pcm_player_L.tick();
      pcm_player_R.tick();
      // draw
      pcm_player_L.draw();
      pcm_player_R.draw();
    }
    return;
  }

  // streaming
  streaming_parser.loop();
  return;

  // rpc board
  rpc_board.loop();
}
