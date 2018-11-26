#include "atomics.h"

namespace gaia {
namespace ipc {

// TODO (danielp): Investigate whether it is possible to relax some of the
// memory ordering requirements in these functions.
bool CompareExchange(volatile uint32_t *value, uint32_t old_val,
                     uint32_t new_val) {
  return __atomic_compare_exchange_n(value, &old_val, new_val, false,
                                     __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

uint32_t ExchangeAdd(volatile uint32_t *dest, volatile int32_t source) {
  return __atomic_fetch_add(dest, source, __ATOMIC_SEQ_CST);
}

uint16_t ExchangeAddWord(volatile uint16_t *dest, volatile int16_t source) {
  return __atomic_fetch_add(dest, source, __ATOMIC_SEQ_CST);
}

uint32_t Exchange(volatile uint32_t *dest, volatile uint32_t source) {
  return __atomic_exchange_n(dest, source, __ATOMIC_SEQ_CST);
}

void BitwiseAnd(volatile uint32_t *dest, uint32_t mask) {
  __atomic_fetch_and(dest, mask, __ATOMIC_SEQ_CST);
}

void Decrement(volatile uint32_t *value) {
  __atomic_fetch_sub(value, 1, __ATOMIC_SEQ_CST);
}

void Increment(volatile uint32_t *value) {
  __atomic_fetch_add(value, 1, __ATOMIC_SEQ_CST);
}

void IncrementWord(volatile uint16_t *value) {
  __atomic_fetch_add(value, 1, __ATOMIC_SEQ_CST);
}

void Fence() {
  __atomic_thread_fence(__ATOMIC_SEQ_CST);
}

}  // namespace ipc
}  // namespace gaia
