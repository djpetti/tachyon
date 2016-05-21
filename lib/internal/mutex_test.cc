#include "gtest/gtest.h"

#include "mutex.h"

namespace gaia {
namespace internal {
namespace testing {

// A test fixture for testing mutexes.
class MutexTest : public ::testing::Test {
 protected:
  virtual void SetUp() {
    // Initialize the mutex.
    mutex_init(&mutex_);
  }

  // This is the mutex we use for testing. We don't even bother to put it in
  // shared memory, because we're just doing basic tests for now.
  // TODO(danielp): Add more fun tests to catch concurrency issues.
  Mutex mutex_;
};

// Tests that we can do basic locking and unlocking from a single thread.
TEST_F(MutexTest, LockUnlockTest) {
  ASSERT_EQ(0u, mutex_.state);

  mutex_grab(&mutex_);
  EXPECT_EQ(1u, mutex_.state);
  mutex_release(&mutex_);
  EXPECT_EQ(0u, mutex_.state);
}

}  // namespace testing
}  // namespace internal
}  // namespace gaia
