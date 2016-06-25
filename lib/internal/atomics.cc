#include "atomics.h"

namespace gaia {
namespace internal {

bool CompareExchange(volatile uint32_t *value, uint32_t old_val,
                     uint32_t new_val) {
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

int32_t ExchangeAdd(volatile int32_t *dest, int32_t source) {
  int32_t original;
  __asm__ __volatile__(
      "lock\n"
      "xaddl %1, %2\n"
      : "=r"(original)
      : "r"(source), "m"(*dest)
      : "memory");
  return original;
}

void Exchange(volatile uint32_t *dest, uint32_t source) {
  __asm__ __volatile__(
      "lock\n"
      "xchgl %2, %1\n"
      : "=m"(*dest)
      : "r"(source), "m"(*dest)
      : "memory");
}

void BitwiseAnd(volatile int32_t *dest, uint32_t mask) {
  __asm__ __volatile__(
      "lock\n"
      "andl %1, %2\n"
      : "=m"(*dest)
      : "r"(mask), "m"(*dest)
      : "memory");
}

void Increment(volatile int32_t *value) {
  __asm__ __volatile__(
      "lock\n"
      "incl %1\n"
      : "=m"(*value)
      : "m"(*value)
      : "memory");
}

void Fence() {
  __asm__ __volatile__(
      "mfence\n"
      :
      :
      :);
}

}  // namespace internal
}  // namespace gaia
