// This sketch designed to work on Arduino GIGA only
// Dual-core PCM streaming: M7 receives serial, M4 outputs to DAC

#include "serial_streaming_lib.h"
#include "dac_output.h"

#include "core_m7.h"
#include "core_m4.h"

using namespace SerialStreamingLibrary;


// M4 DAC wiring and configuration
#if defined(CORE_CM4)
static const int CLOCK_PIN = 6;
static const int DATA_PIN = 5;
static const int LEFT_LATCH_ENABLE_PIN = 8;
static const int RIGHT_LATCH_ENABLE_PIN = 9;
static const int DAC_RESOLUTION = 16;  // bit

DacOutput dac_output(CLOCK_PIN, DATA_PIN, LEFT_LATCH_ENABLE_PIN, RIGHT_LATCH_ENABLE_PIN,
                     DAC_RESOLUTION);
#endif


// Streaming Parser (M7 only)
#if defined(CORE_CM7)
StreamingParser streaming_parser;
#endif


// Arduino

void setup() {
#if defined(CORE_CM7)
  setup_m7();
  // Initialize streaming parser with region availability flags
  streaming_parser.init(&write_trigger_0, &write_trigger_1);
#endif

#if defined(CORE_CM4)
  setup_m4();
#endif
}


void loop() {
#if defined(CORE_CM7)
  // M7: receive serial data, parse packets, write to shared memory
  streaming_parser.loop();
#endif

#if defined(CORE_CM4)
  // M4: read from shared memory, output to DAC
  loop_m4();
#endif
}
