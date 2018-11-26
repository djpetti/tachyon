#ifndef GAIA_LIB_IPC_ATOMICS_H_
#define GAIA_LIB_IPC_ATOMICS_H_

#include <stdint.h>

namespace gaia {

// Uses some assembly magic to perform an atomic compare-and-swap operation on
// a 32-bit int.
// Args:
//  value: The value to check.
//  old_val: The expected value.
//  new_val: The value we want to change the value to.
// Returns:
//  True if the operation succeeded and the value was modified, false if it did
//  not have the expected value and the operation failed.
bool CompareExchange(volatile uint32_t *value, uint32_t old_val,
                     uint32_t new_val);

// Exchanges the source and destination, and then loads the sum of the two into
// the destination. It does this atomically.
// Args:
//  dest: The destination value.
//  source: The source value.
// Returns:
//  The original value of dest before anything was added to it.
uint32_t ExchangeAdd(volatile uint32_t *dest, volatile int32_t source);

// Same thing as the function above, but it operates on a word instead of a
// long.
uint16_t ExchangeAddWord(volatile uint16_t *dest, volatile int16_t source);

// Exchanges the two arguments without doing any comparison.
// Args:
//  dest: The destination value.
//  source: The value to change it to.
// Returns: The old value of the destination.
uint32_t Exchange(volatile uint32_t *dest, volatile uint32_t source);

// Performs an atomic bitwise AND operation on two 32-bit integers.
// Args:
//  dest: The first value to AND. This will be overwritten with the result of
//  the operation.
//  mask: The second value to AND.
void BitwiseAnd(volatile uint32_t *dest, uint32_t mask);

// Perform an atomic decrement operation.
// Args:
//  value: The number to decrement.
void Decrement(volatile uint32_t *value);

// Perform an atomic increment operation.
// Args:
//  value: The value to increment.
void Increment(volatile uint32_t *value);

// Same thing as the function above, but it operates on a word instead of a
// long.
void IncrementWord(volatile uint16_t *value);

// Forces all loads/stores that are before this call to complete before the
// call, and all the ones that are after the call to complete after the call.
// Used to stop the CPU from spontaneouly reordering memory operations in a way
// that breaks lock-free code.
void Fence();

}  // namespace gaia

#endif  // GAIA_LIB_IPC_ATOMICS_H_
