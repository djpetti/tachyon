#include <stdint.h>

#include "gtest/gtest.h"

#include "atomics.h"

namespace gaia {
namespace ipc {
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

// Make sure ExchangeAdd works with negative numbers.
TEST_F(AtomicsTest, ExchangeSubtractTest) {
  uint32_t value = 2;
  EXPECT_EQ(2u, ExchangeAdd(&value, -1));
  EXPECT_EQ(1u, value);
}

// Make sure ExchangeAddWord works properly.
TEST_F(AtomicsTest, ExchangeAddWordTest) {
  uint16_t value = 2;
  EXPECT_EQ(2u, ExchangeAddWord(&value, 1));
  EXPECT_EQ(3u, value);

  EXPECT_EQ(3u, ExchangeAddWord(&value, 2));
  EXPECT_EQ(5u, value);
}

// Make sure ExchangeAddWord allows us to operate on part of a longer integer.
TEST_F(AtomicsTest, ExchangeAddWordPartialTest) {
  uint32_t value = 0xFFFFFFFF;
  uint16_t *value_right = reinterpret_cast<uint16_t *>(&value);
  EXPECT_EQ(0xFFFFu, ExchangeAddWord(value_right, 1));
  EXPECT_EQ(0x0000u, *value_right);
  EXPECT_EQ(0xFFFF0000u, value);
}

// Make sure Exchange woeks as expected.
TEST_F(AtomicsTest, ExchangeTest) {
  uint32_t value = 1;
  uint32_t old_value = Exchange(&value, 2);
  EXPECT_EQ(2u, value);
  EXPECT_EQ(1u, old_value);
}

// Make sure BitwiseAnd works properly.
TEST_F(AtomicsTest, BitwiseAndTest) {
  uint32_t value = 0xFF;
  BitwiseAnd(&value, 0xF0);
  EXPECT_EQ(0xF0u, value);
}

// Make sure Decrement works properly.
TEST_F(AtomicsTest, DecrementTest) {
  uint32_t value = 2;
  Decrement(&value);
  EXPECT_EQ(1u, value);
}

}  // namespace testing
}  // namespace ipc
}  // namespace gaia
