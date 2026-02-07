#ifndef __core_hsem_h__
#define __core_hsem_h__

namespace CoreHSEM {

// ####################
// HSEM
// ####################

#include "stm32h7xx_hal_hsem.h"

static constexpr uint32_t WRITE_HSEM_ID_0 = 28;
static constexpr uint32_t WRITE_HSEM_ID_1 = 29;
static constexpr uint32_t READ_HSEM_ID_0 = 30;
static constexpr uint32_t READ_HSEM_ID_1 = 31;

void hsem_init(const uint32_t hsem_id_0, const uint32_t hsem_id_1, const IRQn_Type irqn) {
  __HAL_RCC_HSEM_CLK_ENABLE();

  uint32_t mask = __HAL_HSEM_SEMID_TO_MASK(hsem_id_0) | __HAL_HSEM_SEMID_TO_MASK(hsem_id_1);
  __HAL_HSEM_CLEAR_FLAG(mask);
  HAL_HSEM_ActivateNotification(mask);

  NVIC_ClearPendingIRQ(irqn);
  NVIC_SetPriority(irqn, 5);
  NVIC_EnableIRQ(irqn);
}

static inline void hsem_trigger(const uint32_t hsem_id) {
  HAL_HSEM_FastTake(hsem_id);
  HAL_HSEM_Release(hsem_id, 0);
}


}  // CoreHSEM

#endif  // !__core_hsem_h__
