#include "gtest/gtest.h"

#include "chunk.h"
#include "lib/ipc/queue.h"

namespace gaia {
namespace testing {
namespace {

constexpr int kChunkSize = 100;

// Populate test data and gradient arrays with a simple sequence.
// Args:
//  data: The data array to populate.
//  gradients: The gradients array to populate.
void MakeTestData(float *data, float *gradients) {
  for (int i = 0; i < kChunkSize; ++i) {
    data[i] = i;
    gradients[i] = i;
  }
}

// Checks that data is the same as the output of MakeTestData().
// Args:
//  data: The data array to check.
//  gradients: The gradients array to check.
void CheckTestData(const float *data, const float *gradients) {
  for (int i = 0; i < kChunkSize; ++i) {
    EXPECT_EQ(i, data[i]);
    EXPECT_EQ(i, gradients[i]);
  }
}

}  // namespace

using ipc::Queue;

// Test fixture for testing the Chunk class.
class ChunkTest : public ::testing::Test {
 protected:
  ChunkTest() : chunk_(kChunkSize) {}

  // Chunk to use for testing.
  Chunk chunk_;
};

// Make sure we can get/set values correctly.
TEST_F(ChunkTest, BasicTest) {
  // Chunk data.
  float test_data[kChunkSize];
  float test_gradients[kChunkSize];
  MakeTestData(test_data, test_gradients);

  // Save it to the chunk.
  chunk_.SetValue(test_data, test_gradients);

  // Read it back.
  const float *chunk_data = chunk_.GetData();
  const float *chunk_gradients = chunk_.GetGradients();
  CheckTestData(chunk_data, chunk_gradients);
}

// Make sure we can enqueue and dequeue chunks.
TEST_F(ChunkTest, EnqueueDequeueTest) {
  // Queue to use for the test.
  Queue<Chunk> queue;

  // Make a test chunk.
  float test_data[kChunkSize];
  float test_gradients[kChunkSize];
  MakeTestData(test_data, test_gradients);
  chunk_.SetValue(test_data, test_gradients);

  // Enqueue it.
  ASSERT_TRUE(chunk_.Enqueue(&queue));

  // Make a new chunk and dequeue it into there.
  Chunk new_chunk(kChunkSize);
  ASSERT_TRUE(new_chunk.Dequeue(&queue));

  // Cleanup queue stuff.
  queue.FreeQueue();
}

// Make sure we can serialize chunks.
TEST_F(ChunkTest, SerializationTest) {
  // Make a test chunk.
  float test_data[kChunkSize];
  float test_gradients[kChunkSize];
  MakeTestData(test_data, test_gradients);
  chunk_.SetValue(test_data, test_gradients);

  // Create a buffer for serialization.
  const int length = chunk_.GetSerializedLength();
  char buffer[length];

  // Serialize it.
  chunk_.Serialize(buffer);

  // Deserialize it.
  Chunk new_chunk(buffer);

  // Make sure it's the same.
  const float *chunk_data = new_chunk.GetData();
  const float *chunk_gradients = new_chunk.GetGradients();
  CheckTestData(chunk_data, chunk_gradients);
}

}  // namespace testing
}  // namespace gaia
