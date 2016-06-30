#include <stdint.h>

#include "gtest/gtest.h"

#include "atomics.h"

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
  int32_t value = 2;
  EXPECT_EQ(2, ExchangeAdd(&value, 1));
  EXPECT_EQ(3, value);

  EXPECT_EQ(3, ExchangeAdd(&value, 2));
  EXPECT_EQ(5, value);
}

// Make sure ExchangeAdd works with negative numbers.
TEST_F(AtomicsTest, ExchangeSubtractTest) {
  int32_t value = 2;
  EXPECT_EQ(2, ExchangeAdd(&value, -1));
  EXPECT_EQ(1, value);
}

// Make sure Exchange woeks as expected.
TEST_F(AtomicsTest, ExchangeTest) {
  uint32_t value = 1;
  Exchange(&value, 2);
  EXPECT_EQ(2u, value);
}

// Make sure BitwiseAnd works properly.
TEST_F(AtomicsTest, BitwiseAndTest) {
  int32_t value = 0xFF;
  BitwiseAnd(&value, 0xF0);
  EXPECT_EQ(0xF0, value);
}

// Make sure Decrement works properly.
TEST_F(AtomicsTest, DecrementTest) {
  int32_t value = 2;
  Decrement(&value);
  EXPECT_EQ(1, value);
}

}  // namespace testing
}  // namespace internal
}  // namespace gaia
