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

int32_t ExchangeAdd(int32_t *dest, int32_t source) {
  int32_t original;
  __asm__ __volatile__(
      "lock\n"
      "xaddl %1, %2\n"
      : "=r"(original)
      : "r"(source), "m"(*dest)
      : "memory");
  return original;
}

void Exchange(uint32_t *dest, uint32_t source) {
  __asm__ __volatile__(
      "lock\n"
      "xchg %1, %0\n"
      :
      : "r"(source), "m"(*dest)
      : "memory");
}

void BitwiseAnd(int32_t *dest, uint32_t mask) {
  __asm__ __volatile__(
      "lock\n"
      "andl %0, %1\n"
      :
      : "r"(mask), "m"(*dest)
      : "memory");
}

}  // namespace internal
}  // namespace gaia
