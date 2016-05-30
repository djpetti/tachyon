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
bool CompareExchange(Futex *futex, int32_t old_val, int32_t new_val) {
  uint8_t ret;
  __asm__ __volatile__("lock\n"
                       "cmpxchgl %2, %1\n"
                       "sete %0\n"
                       : "=q"(ret), "=m"(*futex)
                       : "r"(new_val), "m"(*futex), "a"(old_val)
                       : "memory");
  return ret;
}

// Annoyingly, there is no Glibc wrapper for futex calls, so we have to make the
// syscalls manually.
// Args:
//  futex: The futex we are performing the operation on.
//  futex_op: The futex op we are performing.
//  val: Can mean different things, depending on the op. See the futex
//  documentation for details.
int FutexCall(Futex *futex, int futex_op, int val) {
  return syscall(SYS_futex, futex, futex_op, val, nullptr);
}

}  // namespace

void MutexInit(Mutex *mutex) {
  mutex->state = 0;
}

void MutexGrab(Mutex *mutex) {
  Futex *state = &(mutex->state);

  if (!CompareExchange(state, 0, 1)) {
    // It wasn't zero, which means there's contention and we have to call into
    // the kernel.
    do {
      // We'll assume that the lock is still taken here, and try to set the
      // futex to 2 to indicate contention.
      if (*state == 2 || CompareExchange(state, 1, 2)) {
        // There's still contention. Wait in the kernel.
        int futex_ret = FutexCall(state, FUTEX_WAIT, 2);
        assert((!futex_ret || errno == EAGAIN) &&
               "futex(FUTEX_WAIT) failed unexpectedly.");
      }
    } while (!CompareExchange(state, 0, 2));
    // Someone unlocking it sets it to zero, so we should only get here if we
    // successfully waited until someone unlocked the mutex and then grabbed
    // it.
  }
}

void MutexRelease(Mutex *mutex) {
  Futex *state = &(mutex->state);

  // If the lock is uncontended, this single atomic op is all we need to do to
  // release it.
  if (!CompareExchange(state, 1, 0)) {
    // It can only go up while this function is running, so if the above failed,
    // it must be 2, and we have to wake up someone.
    assert(CompareExchange(state, 2, 0) && "Double-releasing lock?");

    // Wake someone up.
    int futex_ret = FutexCall(state, FUTEX_WAKE, 1);
    assert(futex_ret >= 0 && "futex(FUTEX_WAKE) failed unexpectedly.");
  }
}

}  // namespace internal
}  // namespace gaia
