#include "atomics.h"

namespace gaia {
namespace internal {

bool CompareExchange(uint32_t *value, uint32_t old_val, uint32_t new_val) {
  uint8_t ret;
  __asm__ __volatile__(
      "lock\n"
      "cmpxchgl %2, %1\n"
      "sete %0\n"
      : "=q"(ret), "=m"(*value)
      : "r"(new_val), "m"(*value), "a"(old_val)
      : "memory");
  return ret;
}

uint32_t ExchangeAdd(uint32_t *dest, uint32_t source) {
  uint32_t original;
  __asm__ __volatile__(
      "lock\n"
      "xaddl %1, %2\n"
      : "=r"(original)
      : "r"(source), "m"(*dest)
      : "memory");
  return original;
}

}  // namespace internal
}  // namespace gaia
