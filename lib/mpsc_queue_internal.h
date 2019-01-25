#ifndef TACHYON_LIB_MPSC_QUEUE_INTERNAL_H_
#define TACHYON_LIB_MPSC_QUEUE_INTERNAL_H_

#include <stdint.h>

namespace tachyon {
namespace mpsc_queue {

// Defines functions that are not part of the MpscQueue template, but are used
// internally therein. We do it this way to avoid linker errors.

// Memcpy-like function that handles a volatile destination buffer.
// Args:
//  dest: The destination volatile buffer.
//  src: The source buffer. (Non-volatile).
//  length: How many bytes to copy.
// Returns:
//  The destination address.
volatile void *VolatileCopy(volatile void *__restrict__ dest,
                            const void *__restrict__ src, uint32_t length);

// Calculates the integral base-2 log of an integer.
// Args:
//  input: The number to calculate the log of.
//  log: Set to the log of the number.
// Returns:
//  True if input was a power of 2, false otherwise.
bool IntLog2(uint32_t input, uint8_t *log);

}  // namespace mpsc_queue
}  // namespace tachyon

#endif  // TACHYON_LIB_MPSC_QUEUE_INTERNAL_H_
