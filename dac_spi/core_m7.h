#ifndef __core_m7_h__
#define __core_m7_h__

#if defined(CORE_CM7)

#include "core_hsem.h"
#include "core_mem.h"

using namespace CoreHSEM;
using namespace CoreMemory;


// ####################
// M4 boot
// ####################

static void boot_m4() {
  HAL_RCCEx_EnableBootCore(RCC_BOOT_C2);
  __DSB();
  __ISB();
}


// ####################
// HSEM
// ####################

// true by default
static volatile bool write_trigger_0 = true;
static volatile bool write_trigger_1 = true;

extern "C" void HAL_HSEM_FreeCallback(uint32_t SemMask) {
  // Pre-compute combined mask once
  constexpr uint32_t mask_0 = __HAL_HSEM_SEMID_TO_MASK(WRITE_HSEM_ID_0);
  constexpr uint32_t mask_1 = __HAL_HSEM_SEMID_TO_MASK(WRITE_HSEM_ID_1);
  constexpr uint32_t combined_mask = mask_0 | mask_1;

  // Single mask operation to check relevant bits
  uint32_t relevant = SemMask & combined_mask;
  if (!relevant) return;

  // Batch clear + reactivate for all triggered semaphores at once
  __HAL_HSEM_CLEAR_FLAG(relevant);
  HAL_HSEM_ActivateNotification(relevant);

  if (relevant & mask_0) {
    write_trigger_0 = true;
  }

  if (relevant & mask_1) {
    write_trigger_1 = true;
  }
}


// ####################
// Arduino
// ####################

void setup_m7() {
  // m4 boot
  boot_m4();
  // hsem
  hsem_init(WRITE_HSEM_ID_0, WRITE_HSEM_ID_1, HSEM1_IRQn);  // HSEM1 for M7
  // memory
  reset_memory_regions();
}


#endif  // CORE_CM7

#endif  // !__core_m7_h__
