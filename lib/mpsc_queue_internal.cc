#include "mpsc_queue_internal.h"

namespace tachyon {
namespace mpsc_queue {

volatile void *VolatileCopy(volatile void *__restrict__ dest,
                            const void *__restrict__ src, uint32_t length) {
  const uintptr_t dest_int = reinterpret_cast<const uintptr_t>(dest);
  const uintptr_t src_int = reinterpret_cast<const uintptr_t>(src);

  // Verify 32-bit alignment.
  if (!(dest_int & 0x3) && !(src_int & 0x3)) {
    // Copy in 64-bit increments. Even on 32-bit architectures, the generated
    // code should still be as efficient as copying in 32-bit increments.
    // TODO (danielp): Look into using SSE to accelerate. Right now, the
    // marginal speedup is not worth the extra effort.
    volatile uint64_t *dest_long = reinterpret_cast<volatile uint64_t *>(dest);
    const uint64_t *src_long = reinterpret_cast<const uint64_t *>(src);

    while (length >= 8) {
      *dest_long++ = *src_long++;
      length -= 8;
    }
  }

  volatile uint8_t *dest_byte = reinterpret_cast<volatile uint8_t *>(dest);
  const uint8_t *src_byte = reinterpret_cast<const uint8_t *>(src);

  // Copy remaining or unaligned bytes.
  while (length--) {
    *dest_byte++ = *src_byte++;
  }

  return dest;
}

bool IntLog2(uint32_t input, uint8_t *log) {
  for (*log = 0; *log < 32; ++(*log)) {
    if (input & 0x1) {
      break;
    }
    input >>= 1;
  }

  // If input was a power of two, it should now be 0.
  return input == 1;
}

}  // namespace mpsc_queue
}  // namespace tachyon
