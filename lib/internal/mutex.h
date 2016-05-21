#ifndef GAIA_LIB_INTERNAL_FUTEX_H_
#define GAIA_LIB_INTERNAL_FUTEX_H_

#include <stdint.h>

namespace gaia {
namespace internal {

// Futex documentation requires four-byte alignment, even on 64-bit systems.
typedef uint32_t Futex __attribute__((aligned(4)));

// A low-level mutex implementation. Must be placed in shared memory by whatever
// uses it.
struct Mutex {
  // The actual integer that maintains the futex state.
  // 0 means nobody has the futex, 1 someone has it but it's not contended, and
  // 2 means that someone has it, and there are probably other people waiting
  // for it.
  Futex state;
};

// Initializes a new futex.
// Args:
//  mutex: The mutex to initialize.
void mutex_init(Mutex *mutex);
// Grabs the futex. Will block if the futex has already been grabbed. If there
// is no contention, it does not leave userspace.
// Args:
//  mutex: The mutex to grab.
void mutex_grab(Mutex *mutex);
// Releases the futex, waking the first thing that's waiting on the futex. If
// nobody's waiting, it does not leave userspace.
// Args:
//  mutex: The mutex to release.
void mutex_release(Mutex *mutex);

}  // namespace internal
}  // namespace gaia

#endif // GAIA_LIB_INTERNAL_FUTEX_H_
