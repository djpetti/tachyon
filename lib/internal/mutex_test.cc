#include <functional>
#include <thread>

#include "gtest/gtest.h"

#include "mutex.h"

namespace gaia {
namespace internal {
namespace testing {
namespace {

// A global counter for TestThread to use.
int g_counter = 0;

// A thread for testing mutexes. It basically adds something to a global int
// 10000 times.
// Args:
//  add: The number to add to our counter.
//  mutex: The mutex to use to protect counter operations.
void TestThread(int add, Mutex &mutex) {
  for (int i = 0; i < 10000; ++i) {
    mutex_grab(&mutex);
    g_counter += add;
    mutex_release(&mutex);
  }
}

}  // namespace

// A test fixture for testing mutexes.
class MutexTest : public ::testing::Test {
 protected:
  virtual void SetUp() {
    // Initialize the mutex.
    mutex_init(&mutex_);
  }

  // Mutex for testing. We don't even bother to use shared memory here, since we
  // can just use threads for testing.
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

// Tests that things don't fail or deadlock in a highly concurrent scenario.
TEST_F(MutexTest, StressTest) {
  // Make 8 threads for testing.
  ::std::thread threads[50];
  for (int i = 0; i < 25; ++i) {
    threads[i] = ::std::thread(TestThread, 1, ::std::ref(mutex_));
  }
  for (int i = 25; i < 50; ++i) {
    threads[i] = ::std::thread(TestThread, -1, ::std::ref(mutex_));
  }

  // Wait for everything to finish.
  for (int i = 0; i < 50; ++i) {
    threads[i].join();
  }

  // Our counter should be zero after the dust clears.
  EXPECT_EQ(0, g_counter);
}

}  // namespace testing
}  // namespace internal
}  // namespace gaia
