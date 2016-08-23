#include "gtest/gtest.h"

#include "shared_hashmap.h"

namespace gaia {
namespace internal {
namespace testing {

// Test fixture for testing the SharedHashmap class.
class SharedHashmapTest : public ::testing::Test {
 protected:
  // Map offset.
  static constexpr int kOffset = 0;
  // Map size.
  static constexpr int kSize = 128;

  virtual void SetUp() {
    // Clear the pool in between, so tests don't affect each-other.
    Pool::GetPool()->Clear();

    map_ = new SharedHashmap<const char *, int>(kOffset, kSize);
  }

  virtual void TearDown() {
    map_->Free();
    delete map_;
  }

  static void TearDownTestCase() {
    // Unlink SHM.
    ASSERT_TRUE(Pool::Unlink());
  }

  // SharedHashmap instance to use for testing.
  SharedHashmap<const char *, int> *map_;
};

// Make sure we can add and find items in the map.
TEST_F(SharedHashmapTest, BasicTest) {
  map_->AddOrSet("correct", 0);
  map_->AddOrSet("horse", 1);

  // Make sure we can get them back.
  int result;
  ASSERT_TRUE(map_->Fetch("correct", &result));
  EXPECT_EQ(0, result);
  ASSERT_TRUE(map_->Fetch("horse", &result));
  EXPECT_EQ(1, result);

  // Try changing their value.
  map_->AddOrSet("correct", 2);
  map_->AddOrSet("horse", 3);

  ASSERT_TRUE(map_->Fetch("correct", &result));
  EXPECT_EQ(2, result);
  ASSERT_TRUE(map_->Fetch("horse", &result));
  EXPECT_EQ(3, result);
}

// Make sure it handles things not being in the map.
TEST_F(SharedHashmapTest, NonexistentTest) {
  int result;
  EXPECT_FALSE(map_->Fetch("battery", &result));
}

// Strings as keys are handled separately, so it's worth making sure that we can
// successfully use something else as a key as well.
TEST_F(SharedHashmapTest, NonStringKeyTest) {
  SharedHashmap<int, int> map(5000, 128);

  map.AddOrSet(5, 6);

  int result;
  ASSERT_TRUE(map.Fetch(5, &result));
  EXPECT_EQ(6, result);

  // Change the value.
  map.AddOrSet(5, 7);

  ASSERT_TRUE(map.Fetch(5, &result));
  EXPECT_EQ(7, result);
}

// Make sure it handles buckets with multiple items.
TEST_F(SharedHashmapTest, OveruseTest) {
  // We'll put kSize + 1 items in the map, which will force at least one bucket
  // to have two items.
  const ::std::string base_key = "duck";
  for (int i = 0; i < kSize; ++i) {
    const ::std::string key = base_key + ::std::to_string(i);
    map_->AddOrSet(key.c_str(), i);
  }

  // Now make sure we can read every item back.
  for (int i = 0; i < kSize; ++i) {
    const ::std::string key = base_key + ::std::to_string(i);
    int result;
    ASSERT_TRUE(map_->Fetch(key.c_str(), &result));
    EXPECT_EQ(i, result);
  }
}

}  // namespace testing
}  // namespace internal
}  // namespace gaia
