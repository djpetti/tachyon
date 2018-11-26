#ifndef TACHYON_LIB_IPC_MUTEX_H_
#define TACHYON_LIB_IPC_MUTEX_H_

#include <stdint.h>

namespace tachyon {

// TODO (danielp): Priority-inversion futex calls.

// Futex documentation requires four-byte alignment, even on 64-bit systems.
typedef volatile uint32_t Futex __attribute__((aligned(4)));

// A low-level mutex implementation. Must be placed in shared memory by whatever
// uses it.
struct Mutex {
  // The actual integer that maintains the futex state.
  // 0 means nobody has the futex, 1 someone has it but it's not contended, and
  // 2 means that someone has it, and there are probably other people waiting
  // for it.
  Futex state;
};

// A wrapper for FUTEX_WAIT calls.
// Args:
//  futex: The futex to wait on.
//  expected: The expected value of the futex.
// Returns: True if the futex call succeeded normally, false if it exited
// immediately with EAGAIN. (Meaning the condition was not true.)
bool FutexWait(Futex *futex, int expected);
// A wrapper for FUTEX_WAKE calls.
// Args:
//  futex: The futex to wake waiters on.
//  num_waiters: How many waiters to wake.
// Returns:
//  The number of waiters it woke up.
int FutexWake(Futex *futex, int num_waiters);

// Initializes a new futex.
// Args:
//  mutex: The mutex to initialize.
void MutexInit(Mutex *mutex);
// Grabs the futex. Will block if the futex has already been grabbed. If there
// is no contention, it does not leave userspace.
// Args:
//  mutex: The mutex to grab.
void MutexGrab(Mutex *mutex);
// Releases the futex, waking the first thing that's waiting on the futex. If
// nobody's waiting, it does not leave userspace.
// Args:
//  mutex: The mutex to release.
void MutexRelease(Mutex *mutex);

}  // namespace tachyon

#endif // TACHYON_LIB_IPC_MUTEX_H_
