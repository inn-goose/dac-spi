#ifndef __core_m4_h__
#define __core_m4_h__

#if defined(CORE_CM4)

#include "core_hsem.h"
#include "core_mem.h"

using namespace CoreHSEM;
using namespace CoreMemory;

// DacOutput instance defined in dac_spi.ino
extern DacOutput dac_output;


// ####################
// HSEM
// ####################

// false by default
static volatile bool read_trigger_0 = false;
static volatile bool read_trigger_1 = false;

extern "C" void HAL_HSEM_FreeCallback(uint32_t SemMask) {
  // Pre-compute combined mask once
  constexpr uint32_t mask_0 = __HAL_HSEM_SEMID_TO_MASK(READ_HSEM_ID_0);
  constexpr uint32_t mask_1 = __HAL_HSEM_SEMID_TO_MASK(READ_HSEM_ID_1);
  constexpr uint32_t combined_mask = mask_0 | mask_1;

  // Single mask operation to check relevant bits
  uint32_t relevant = SemMask & combined_mask;
  if (!relevant) return;

  // Batch clear + reactivate for all triggered semaphores at once
  __HAL_HSEM_CLEAR_FLAG(relevant);
  HAL_HSEM_ActivateNotification(relevant);

  if (relevant & mask_0) {
    read_trigger_0 = true;
  }

  if (relevant & mask_1) {
    read_trigger_1 = true;
  }
}


// ####################
// Arduino
// ####################

static void blink(int led, int delay_ms) {
  const int one_phase_ms = 100;
  pinMode(led, OUTPUT);
  for (int i = 0; i < int(delay_ms / one_phase_ms); i++) {
    digitalWrite(led, LOW);
    delay(int(one_phase_ms / 2));
    digitalWrite(led, HIGH);
    delay(int(one_phase_ms / 2));
  }
}

void setup_m4() {
  // m4 boot indicator
  blink(LEDB, 200);
  // hsem
  hsem_init(READ_HSEM_ID_0, READ_HSEM_ID_1, HSEM2_IRQn);  // HSEM2 for M4
  // dac
  dac_output.setup();
}


int read_memory_region_id = 0;

void loop_m4() {
  if (read_memory_region_id == 0 && read_trigger_0) {
    read_trigger_0 = false;

    // Read header from shared memory
    SharedRegionHeader* header = get_region_header(0);
    uint32_t n_channels = header->n_channels;
    uint32_t samples_count = header->samples_count;

    // Get samples pointer and start playback (non-blocking)
    volatile int16_t* samples = get_region_samples(0);
    dac_output.start_playback(n_channels, samples_count, samples, 0);

    // Wait for playback to complete
    while (dac_output.is_busy()) {
      __NOP();
    }

    // Signal M7 that region 0 is free
    hsem_trigger(WRITE_HSEM_ID_0);

    read_memory_region_id = 1;

  } else if (read_memory_region_id == 1 && read_trigger_1) {
    read_trigger_1 = false;

    // Read header from shared memory
    SharedRegionHeader* header = get_region_header(1);
    uint32_t n_channels = header->n_channels;
    uint32_t samples_count = header->samples_count;

    // Get samples pointer and start playback (non-blocking)
    volatile int16_t* samples = get_region_samples(1);
    dac_output.start_playback(n_channels, samples_count, samples, 1);

    // Wait for playback to complete
    while (dac_output.is_busy()) {
      __NOP();
    }

    // Signal M7 that region 1 is free
    hsem_trigger(WRITE_HSEM_ID_1);

    read_memory_region_id = 0;
  }
}


#endif  // CORE_CM4

#endif  // !__core_m4_h__
