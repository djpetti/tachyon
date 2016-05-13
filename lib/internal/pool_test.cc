#include <stdio.h>
#include <sys/mman.h>

#include "gtest/gtest.h"

#include "pool.h"

namespace gaia {
namespace internal {
namespace testing {

// Test fixture for testing the pool class.
class PoolTest : public ::testing::Test {
 protected:
  static const int kPoolSize = 1000;

  virtual void SetUp() {
    // We're going to end by deleting the SHM. If there are contents left over
    // there from previous runs, it could affect things.
    const int unlink_ret = shm_unlink("/gaia_core");
    if (unlink_ret && errno != ENOENT) {
      FAIL();
    }

    // Pool is allocated on the heap so we can force initialization in the
    // correct order.
    pool_ = new Pool(kPoolSize);
  }

  virtual void TearDown() { delete pool_; }

  // Pool instance to use for testing.
  Pool *pool_;
};

// Make sure we can allocate shared memory from the pool.
TEST_F(PoolTest, AllocationTest) {
  int *shared_int = pool_->AllocateForType<int>();
  *shared_int = 42;

  int *another_int = pool_->AllocateForType<int>();
  *another_int = 1337;
  // Make sure it gave us different blocks.
  EXPECT_EQ(42, *shared_int);
  EXPECT_EQ(1337, *another_int);

  // Also make sure that they're one block apart.
  EXPECT_EQ(128u, (another_int - shared_int) * sizeof(int));
}

// Make sure it behaves properly when we run out of pool memory.
TEST_F(PoolTest, OverusedPoolTest) {
  // We'll have ask for 8 separate blocks to use up all our memory.
  for (int i = 0; i < 8; ++i) {
    EXPECT_NE(nullptr, pool_->AllocateForType<int>());
  }

  // Now, our next allocation should fail.
  EXPECT_EQ(nullptr, pool_->AllocateForType<int>());
}

// Make sure we can free objects as well as allocate them.
TEST_F(PoolTest, FreeTest) {
  int *shared_int = pool_->AllocateForType<int>();
  *shared_int = 42;
  pool_->FreeType<int>(shared_int);

  int *another_int = pool_->AllocateForType<int>();
  *another_int = 1337;
  // It should have re-used the memory that was previously allocated.
  EXPECT_EQ(another_int, shared_int);
}

// Make sure that different pool instances stay in sync.
TEST_F(PoolTest, SharedTest) {
  // Allocate some data in our current pool.
  int *shared_int = pool_->AllocateForType<int>();
  *shared_int = 42;

  // Create a new pool.
  Pool new_pool(kPoolSize);
  // Make sure it allocates a new block.
  int *another_int = new_pool.AllocateForType<int>();
  *another_int = 1337;
  // It's not enough to compare pointers, because they could be mapped at
  // different regions.
  EXPECT_EQ(42, *shared_int);
  EXPECT_EQ(1337, *another_int);
}

// Make sure that allocating a region spanning multiple blocks works.
TEST_F(PoolTest, MultiBlockAllocationTest) {
  int *shared_array = pool_->AllocateForArray<int>(64);
  // Make sure we can write to every single element.
  for (int i = 0; i < 64; ++i) {
    shared_array[i] = i;
  }

  // Make sure we can still allocate more without overlapping.
  int *shared_int = pool_->AllocateForType<int>();
  *shared_int = 1337;
  for (int i = 0; i < 64; ++i) {
    EXPECT_EQ(i, shared_array[i]);
  }
}

}  // namespace testing
}  // namespace internal
}  // namespace gaia
