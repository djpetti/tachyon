#include "atomics.h"

namespace gaia {
namespace ipc {

bool CompareExchange(volatile uint32_t *value, uint32_t old_val,
                     uint32_t new_val) {
  volatile uint8_t ret;
  __asm__ __volatile__(
      "lock\n"
      "cmpxchgl %2, %1\n"
      "sete %0\n"
      : "=q"(ret), "=m"(*value)
      : "r"(new_val), "m"(*value), "a"(old_val)
      : "memory");
  return ret;
}

uint32_t ExchangeAdd(volatile uint32_t *dest, volatile int32_t source) {
  __asm__ __volatile__(
      "lock\n"
      "xaddl %2, %3\n"
      : "=r"(source), "=m"(*dest)
      : "r"(source), "m"(*dest)
      : "memory");
  return source;
}

uint16_t ExchangeAddWord(volatile uint16_t *dest, volatile int16_t source) {
  __asm__ __volatile__(
      "lock\n"
      "xaddw %2, %3\n"
      : "=r"(source), "=m"(*dest)
      : "r"(source), "m"(*dest)
      : "memory");
  return source;
}

uint32_t Exchange(volatile uint32_t *dest, volatile uint32_t source) {
  __asm__ __volatile__(
      "lock\n"
      "xchgl %1, %0\n"
      : "=r"(source), "=m"(*dest)
      : "r"(source), "m"(*dest)
      : "memory");
  return source;
}

void BitwiseAnd(volatile uint32_t *dest, uint32_t mask) {
  __asm__ __volatile__(
      "lock\n"
      "andl %1, %2\n"
      : "=m"(*dest)
      : "r"(mask), "m"(*dest)
      : "memory");
}

void Decrement(volatile uint32_t *value) {
  __asm__ __volatile__(
      "lock\n"
      "decl %1\n"
      : "=m"(*value)
      : "m"(*value)
      : "memory");
}

void Increment(volatile uint32_t *value) {
  __asm__ __volatile__(
      "lock\n"
      "incl %1\n"
      : "=m"(*value)
      : "m"(*value)
      : "memory");
}

void IncrementWord(volatile uint16_t *value) {
  __asm__ __volatile__(
      "lock\n"
      "incw %1\n"
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

}  // namespace ipc
}  // namespace gaia
