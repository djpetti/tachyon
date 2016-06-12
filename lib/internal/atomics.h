#ifndef GAIA_LIB_INTERNAL_ATOMICS_H_
#define GAIA_LIB_INTERNAL_ATOMICS_H_

#include <stdint.h>

namespace gaia {
namespace internal {

// Uses some assembly magic to perform an atomic compare-and-swap operation on
// a 32-bit int.
// Args:
//  value: The value to check.
//  old_val: The expected value.
//  new_val: The value we want to change the value to.
// Returns:
//  True if the operation succeeded and the value was modified, false if it did
//  not have the expected value and the operation failed.
bool CompareExchange(uint32_t *value, uint32_t old_val, uint32_t new_val);

// Exchanges the source and destination, and then loads the sum of the two into
// the destination. It does this atomically.
// Args:
//  dest: The destination value.
//  source: The source value.
// Returns:
//  The original value of dest before anything was added to it.
uint32_t ExchangeAdd(uint32_t *dest, uint32_t source);

}  // namespace internal
}  // namespace gaia

#endif  // GAIA_LIB_INTERNAL_ATOMICS_H_
