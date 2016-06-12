#include "atomics.h"

#include "gtest/gtest.h"

namespace gaia {
namespace internal {
namespace testing {

// A test fixture for testing atomic functions.
class AtomicsTest : public ::testing::Test {};

// Make sure CompareExchange works properly.
TEST_F(AtomicsTest, CompareExchangeTest) {
  // Do one where it equals the expected value.
  uint32_t value = 1;
  EXPECT_TRUE(CompareExchange(&value, 1, 2));
  EXPECT_EQ(2u, value);

  // Do one where it doesn't equal the expected value.
  EXPECT_FALSE(CompareExchange(&value, 1, 0));
  EXPECT_EQ(2u, value);
}

// Make sure ExchangeAdd works properly.
TEST_F(AtomicsTest, ExchangeAddTest) {
  uint32_t value = 2;
  EXPECT_EQ(2u, ExchangeAdd(&value, 1));
  EXPECT_EQ(3u, value);

  EXPECT_EQ(3u, ExchangeAdd(&value, 2));
  EXPECT_EQ(5u, value);
}

}  // namespace testing
}  // namespace internal
}  // namespace gaia
