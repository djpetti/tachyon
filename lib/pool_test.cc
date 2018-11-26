#include <stdio.h>

#include "gtest/gtest.h"

#include "constants.h"
#include "pool.h"

namespace tachyon {
namespace testing {

// Test fixture for testing the pool class.
class PoolTest : public ::testing::Test {
 protected:
  PoolTest() : pool_(Pool::GetPool()) {}

  virtual void SetUp() {
    // Clear the pool in between, so tests don't affect each-other.
    pool_->Clear();
  }

  static void TearDownTestCase() {
    // Unlink SHM.
    ASSERT_TRUE(Pool::Unlink());
  }

  // Pool instance to use for testing.
  Pool *pool_;
};

// Make sure we can allocate shared memory from the pool.
TEST_F(PoolTest, AllocationTest) {
  int *shared_int = pool_->AllocateForType<int>();
  ASSERT_NE(nullptr, shared_int);
  *shared_int = 42;

  int *another_int = pool_->AllocateForType<int>();
  ASSERT_NE(nullptr, another_int);
  *another_int = 1337;
  // Make sure it gave us different blocks.
  EXPECT_EQ(42, *shared_int);
  EXPECT_EQ(1337, *another_int);

  // Also make sure that they're one block apart.
  EXPECT_EQ(128u, (another_int - shared_int) * sizeof(int));
}

// Make sure it behaves properly when we run out of pool memory.
TEST_F(PoolTest, OverusedPoolTest) {
  // We'll have ask for enough separate blocks to use up all our memory.
  const int total_blocks = pool_->get_size() / pool_->get_block_size();
  for (int i = 0; i < total_blocks; ++i) {
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

// Test that AllocateAt() works.
TEST_F(PoolTest, PlacementAllocationTest) {
  // Allocate memory at a specific offset.
  void *reserved = pool_->AllocateAt(42, 1000);
  ASSERT_NE(nullptr, reserved);

  // Make sure it actually put it at the right spot.
  EXPECT_EQ(42, pool_->GetOffset(reserved));
}

// Make sure normal allocations work around placement allocations.
TEST_F(PoolTest, NonOverlapTest) {
  // Allocate memory at a specific offset.
  const int offset = pool_->get_block_size() + 1;
  uint8_t *reserved = pool_->AllocateAt(offset, 1000);
  ASSERT_NE(nullptr, reserved);

  // We should have a block before it to put something in.
  uint8_t *before = pool_->Allocate(4);
  ASSERT_NE(nullptr, reserved);
  EXPECT_LT(before, reserved);

  // Now anything else should come after it.
  uint8_t *after = pool_->Allocate(4);
  ASSERT_NE(nullptr, after);
  EXPECT_GT(after, reserved);

  // If we free our original allocated memory, it should be able to put
  // something else in that space.
  pool_->Free(reserved, 1000);
  uint8_t *overlapping = pool_->Allocate(4);
  ASSERT_NE(nullptr, overlapping);
  // We asked for reserved to be one byte into the block, and it's going to put
  // overlapping at the beginning of the block.
  EXPECT_EQ(reserved - 1, overlapping);

  // Now it should not give us that spot again, because someone's taken it in
  // the meantime.
  reserved = pool_->AllocateAt(offset, 1000);
  EXPECT_EQ(nullptr, reserved);
}

// Make sure the IsMemoryUsed() method works.
TEST_F(PoolTest, IsMemoryUsedTest) {
  // Initially, no memory should be used.
  EXPECT_FALSE(pool_->IsMemoryUsed(0));

  // Put something there.
  uint8_t *reserved = pool_->AllocateAt(0, 1);
  ASSERT_NE(nullptr, reserved);

  // Now, it should reflect that.
  EXPECT_TRUE(pool_->IsMemoryUsed(0));
}

}  // namespace testing
}  // namespace tachyon
