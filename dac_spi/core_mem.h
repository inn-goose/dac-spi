#ifndef __core_mem_h__
#define __core_mem_h__

namespace CoreMemory {

// ####################
// Memory
// ####################

static constexpr size_t MEMORY_REGION_SIZE = 32 * 1024;
static constexpr size_t MEMORY_REGION_INT16_COUNT = MEMORY_REGION_SIZE / sizeof(int16_t);
static constexpr size_t TOTAL_SHARED_SIZE = MEMORY_REGION_SIZE * 2;

// consumes total 64K of D4 region
static constexpr uint32_t MEMORY_REGION_ADDRESS_0 = 0x38000000UL;
static constexpr uint32_t MEMORY_REGION_ADDRESS_1 = 0x38008000UL;

static volatile uint8_t* const memory_region_0 = (volatile uint8_t*)MEMORY_REGION_ADDRESS_0;
static volatile uint8_t* const memory_region_1 = (volatile uint8_t*)MEMORY_REGION_ADDRESS_1;

// ####################
// Shared Region Header
// ####################

// Header at start of each region for inter-core metadata
struct __attribute__((aligned(32))) SharedRegionHeader {
  uint32_t n_channels;     // 1 (mono) or 2 (stereo)
  uint32_t samples_count;  // total int16_t samples in payload
};

static constexpr size_t HEADER_SIZE = 32;  // cache-line aligned
static constexpr size_t SAMPLES_OFFSET = HEADER_SIZE;
static constexpr size_t MAX_SAMPLES_PER_REGION = (MEMORY_REGION_SIZE - HEADER_SIZE) / sizeof(int16_t);  // 16368

static inline volatile uint8_t* get_memory_region(int region_id) {
  return (region_id == 0) ? memory_region_0 : memory_region_1;
}

static inline SharedRegionHeader* get_region_header(int region_id) {
  return (SharedRegionHeader*)get_memory_region(region_id);
}

static inline volatile int16_t* get_region_samples(int region_id) {
  return (volatile int16_t*)(get_memory_region(region_id) + SAMPLES_OFFSET);
}

// ####################
// Conversions
// ####################

// Optimized volatile-aware conversions using 32-bit operations
static inline void uint8_to_int16(
  volatile uint8_t* src,
  int16_t* dst,
  size_t count) {
  // Copy as 32-bit words (2x int16_t at once) for better performance
  volatile uint32_t* src32 = (volatile uint32_t*)src;
  uint32_t* dst32 = (uint32_t*)dst;
  size_t word_count = count / 2;

  for (size_t i = 0; i < word_count; i++) {
    dst32[i] = src32[i];
  }

  // Handle odd count
  if (count & 1) {
    volatile int16_t* src16 = (volatile int16_t*)src;
    dst[count - 1] = src16[count - 1];
  }
}

static inline void int16_to_uint8(
  const int16_t* src,
  volatile uint8_t* dst,
  size_t count) {
  // Copy as 32-bit words (2x int16_t at once) for better performance
  const uint32_t* src32 = (const uint32_t*)src;
  volatile uint32_t* dst32 = (volatile uint32_t*)dst;
  size_t word_count = count / 2;

  for (size_t i = 0; i < word_count; i++) {
    dst32[i] = src32[i];
  }

  // Handle odd count
  if (count & 1) {
    volatile int16_t* dst16 = (volatile int16_t*)dst;
    dst16[count - 1] = src[count - 1];
  }
}

#if defined(CORE_CM7)

static inline void cleanDCache(void* addr, size_t size) {
  uint32_t start = (uint32_t)addr & ~0x1FU;
  uint32_t length = (uint32_t)size + ((uint32_t)addr - start);
  length = (length + 31U) & ~31U;
  SCB_CleanDCache_by_Addr((uint32_t*)start, length);
  __DMB();
}

static inline void invalidateDCache(void* addr, size_t size) {
  uint32_t start = (uint32_t)addr & ~0x1FU;
  uint32_t length = (uint32_t)size + ((uint32_t)addr - start);
  length = (length + 31U) & ~31U;
  SCB_InvalidateDCache_by_Addr((uint32_t*)start, (int32_t)length);
  __DMB();
}

void reset_memory_regions() {
  // Use 32-bit writes for 4x faster zeroing
  volatile uint32_t* mem32_0 = (volatile uint32_t*)memory_region_0;
  volatile uint32_t* mem32_1 = (volatile uint32_t*)memory_region_1;

  const size_t word_count = MEMORY_REGION_SIZE / sizeof(uint32_t);
  for (size_t i = 0; i < word_count; i++) {
    mem32_0[i] = 0;
    mem32_1[i] = 0;
  }

  // All 4 regions are contiguous: single cache clean for entire 64KB
  cleanDCache((void*)memory_region_0, TOTAL_SHARED_SIZE);
}

#endif  // CORE_CM7


}  // CoreMemory

#endif  // !__core_mem_h__
