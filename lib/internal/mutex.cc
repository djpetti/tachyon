#include "mutex.h"

#include <assert.h>
#include <errno.h>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace gaia {
namespace internal {
namespace {

// Uses some assembly magic to perform an atomic compare-and-swap operation on
// the futex.
// Args:
//  futex: The futex to check.
//  old_val: The expected value of the futex.
//  new_val: The value we want to change the futex to.
// Returns:
//  True if the operation succeeded and the futex was modified, false if it did
//  not have the expected value and the operation failed.
bool compare_exchange(Futex *futex, int32_t old_val, int32_t new_val) {
  uint8_t ret;
  __asm__ __volatile__("lock\n"
                       "cmpxchgl %2, %1\n"
                       "sete %0\n"
                       : "=q"(ret), "=m"(*futex)
                       : "r"(new_val), "m"(*futex), "a"(old_val)
                       : "memory");
  return ret;
}

// Atomically decrements an integer.
// Args:
//  futex: The futex to decrement.
void atomic_decrement(Futex *futex) {
  __asm__ __volatile__("lock\n"
                       "decl %0\n"
                       :
                       : "m"(*futex)
                       : "memory" );
}

// Annoyingly, there is no Glibc wrapper for futex calls, so we have to make the
// syscalls manually.
// Args:
//  futex: The futex we are performing the operation on.
//  futex_op: The futex op we are performing.
//  val: Can mean different things, depending on the op. See the futex
//  documentation for details.
int futex(Futex *futex, int futex_op, int val) {
  return syscall(SYS_futex, futex, futex_op, val);
}

}  // namespace

void mutex_init(Mutex *mutex) {
  mutex->state = 0;
}

void mutex_grab(Mutex *mutex) {
  Futex *state = &(mutex->state);

  if (!compare_exchange(state, 0, 1)) {
    // It wasn't zero, which means there's contention and we have to call into
    // the kernel.
    do {
      // We'll assume that the lock is still taken here, and try to set the
      // futex to 2 to indicate contention.
      if (*state == 2 || compare_exchange(state, 1, 2)) {
        // There's still contention. Wait in the kernel.
        int futex_ret = futex(state, FUTEX_WAIT, 2);
        assert((!futex_ret || futex_ret == EAGAIN) &&
               "futex(FUTEX_WAIT) failed unexpectedly.");
      }
    } while (!compare_exchange(state, 0, 2));
    // Someone unlocking it sets it to zero, so we should only get here if we
    // successfully waited until someone unlocked the mutex and then grabbed
    // it.
  }
}

void mutex_release(Mutex *mutex) {
  Futex *state = &(mutex->state);
  assert(*state && "Releasing an unlocked mutex??");

  atomic_decrement(state);
  if (*state == 1) {
    // It was 2.
    // Actually release the lock.
    *state = 0;
    // Wake someone up.
    int futex_ret = futex(state, FUTEX_WAKE, 1);
    assert(futex_ret >= 0 && "futex(FUTEX_WAKE) failed unexpectedly.");
  }
}

}  // namespace internal
}  // namespace gaia
