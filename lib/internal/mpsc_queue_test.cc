#include <future>
#include <thread>

#include <gtest/gtest.h>

#include "constants.h"
#include "pool.h"
#include "mpsc_queue.h"

namespace gaia {
namespace internal {
namespace testing {
namespace {

// A queue producer thread. It just runs in a loop and sticks a
// sequence on the queue.
// Args:
//  queue: The queue to use.
void ProducerThread(MpscQueue<int> *queue) {
  for (int i = -3000; i <= 3000; ++i) {
    // We're doing non-blocking tests here, so we basically just spin around
    // until it works.
    while (!queue->Enqueue(i));
  }
}

// Does the exact same thing as the function above, but uses blocking.
void BlockingProducerThread(MpscQueue<int> *queue) {
  for (int i = -6000; i <= 6000; ++i) {
    queue->EnqueueBlocking(i);
  }
}

// Does the same thing as the functions above, but alternates between blocking
// and non-blocking writes.
void AlternatingProducerThread(MpscQueue<int> *queue) {
  for (int i = -6000; i <= 6000; ++i) {
    if (i % 2) {
      queue->EnqueueBlocking(i);
    } else {
      while (!queue->Enqueue(i));
    }
  }
}

// A queue consumer thread. It just runs in a loop and reads a sequence off the
// queue. The sequence is verified by checking that it sums to zero.
// Args:
//  queue: The queue to use.
//  num_producers: The number of producers we have.
// Returns:
//  The sum of everything it read off the queues.
int ConsumerThread(MpscQueue<int> *queue, int num_producers) {
  int total = 0;
  for (int i = 0; i < 6001 * num_producers; ++i) {
    int compare;
    while (!queue->DequeueNext(&compare));
    total += compare;
  }

  return total;
}

// Does the exact same thing as the function above, but uses blocking.
int BlockingConsumerThread(MpscQueue<int> *queue, int num_producers) {
  int total = 0;
  for (int i = 0; i < 12001 * num_producers; ++i) {
    int compare;
    queue->DequeueNextBlocking(&compare);
    total += compare;
  }

  return total;
}

// Does the same thing as the functions above, but alternates between blocking
// and non-blocking reads.
int AlternatingConsumerThread(MpscQueue<int> *queue, int num_producers) {
  int total = 0;
  for (int i = 0; i < 12001 * num_producers; ++i) {
    int compare;
    if (i % 2) {
      queue->DequeueNextBlocking(&compare);
    } else {
      while (!queue->DequeueNext(&compare));
    }
    total += compare;
  }

  return total;
}

}  // namespace

// Tests for the queue.
class MpscQueueTest : public ::testing::Test {
 protected:
  MpscQueueTest() = default;

  virtual void SetUp() {
    // Clear the pool for this process. This is where the queues store their
    // state, so clearing this ensures that tests don't affect each-other.
    Pool::GetPool(kPoolSize)->Clear();
  }

  static void TearDownTestCase() {
    // Unlink SHM.
    ASSERT_TRUE(Pool::Unlink());
  }

  // The queue we are testing with.
  MpscQueue<int> queue_;
};

// Test that we can enqueue items properly.
TEST_F(MpscQueueTest, EnqueueTest) {
  // Fill up the entire queue.
  for (int i = 0; i < kQueueCapacity; ++i) {
    EXPECT_TRUE(queue_.Enqueue(i));
  }

  // Now it shouldn't let us do any more.
  EXPECT_FALSE(queue_.Enqueue(51));
}

// Test that we can dequeue items properly.
TEST_F(MpscQueueTest, DequeueTest) {
  // Put some items on the queue.
  for (int i = 0; i < 10; ++i) {
    ASSERT_TRUE(queue_.Enqueue(i));
  }

  // Try dequeing stuff.
  int on_queue;
  for (int i = 0; i < 10; ++i) {
    EXPECT_TRUE(queue_.DequeueNext(&on_queue));
    EXPECT_EQ(on_queue, i);
  }

  // There should be nothing else.
  EXPECT_FALSE(queue_.DequeueNext(&on_queue));
}

// Test that we can use the queue normally in a single-threaded case.
TEST_F(MpscQueueTest, SingleThreadTest) {
  int dequeue_counter = 0;
  int on_queue;
  for (int i = 0; i < 20; i += 2) {
    // Here, we'll enqueue two items and deque one.
    EXPECT_TRUE(queue_.Enqueue(i));
    EXPECT_TRUE(queue_.Enqueue(i + 1));

    EXPECT_TRUE(queue_.DequeueNext(&on_queue));
    EXPECT_EQ(dequeue_counter, on_queue);
    ++dequeue_counter;
  }

  // Now dequeue everything remaining.
  for (int i = 0; i < 10; ++i) {
    EXPECT_TRUE(queue_.DequeueNext(&on_queue));
    EXPECT_EQ(dequeue_counter, on_queue);
    ++dequeue_counter;
  }

  // There should be nothing left.
  EXPECT_FALSE(queue_.DequeueNext(&on_queue));
}

// Test that we can use the queue normally with two threads.
TEST_F(MpscQueueTest, SpscTest) {
  ::std::thread producer(ProducerThread, &queue_);
  ::std::future<int> consumer_ret = ::std::async(&ConsumerThread, &queue_, 1);

  // Wait for them both to finish.
  EXPECT_EQ(0, consumer_ret.get());
  producer.join();
}

// Test that we can use the queue normally with lots of threads.
TEST_F(MpscQueueTest, MpscTest) {
  ::std::thread producers[50];
  ::std::future<int> consumer = ::std::async(&ConsumerThread, &queue_, 50);

  // Make 50 producers, all using the same queue.
  for (int i = 0; i < 50; ++i) {
    producers[i] = ::std::thread(ProducerThread, &queue_);
  }

  // Everything should sum to zero.
  EXPECT_EQ(0, consumer.get());

  // Join all the producers.
  for (int i = 0; i < 50; ++i) {
    producers[i].join();
  }
}

// Tests that we can use the queue normally in a single-threaded case with
// blocking.
TEST_F(MpscQueueTest, SingleThreadBlockingTest) {
  int dequeue_counter = 0;
  int on_queue;
  for (int i = 0; i < 20; i += 2) {
    // Here, we'll enqueue two items and deque one.
    queue_.EnqueueBlocking(i);
    queue_.EnqueueBlocking(i + 1);

    queue_.DequeueNextBlocking(&on_queue);
    EXPECT_EQ(dequeue_counter, on_queue);
    ++dequeue_counter;
  }

  // Now dequeue everything remaining.
  for (int i = 0; i < 10; ++i) {
    queue_.DequeueNextBlocking(&on_queue);
    EXPECT_EQ(dequeue_counter, on_queue);
    ++dequeue_counter;
  }

  // There should be nothing left.
  EXPECT_FALSE(queue_.DequeueNext(&on_queue));
}

// Test that we can use the queue normally with two threads and blocking.
TEST_F(MpscQueueTest, SpscBlockingTest) {
  ::std::thread producer(BlockingProducerThread, &queue_);
  ::std::future<int> consumer_ret =
      ::std::async(&BlockingConsumerThread, &queue_, 1);

  // Wait for them both to finish.
  EXPECT_EQ(0, consumer_ret.get());
  producer.join();
}

// Test that we can use the queue normally with lots of threads and blocking.
TEST_F(MpscQueueTest, MpscBlockingTest) {
  ::std::thread producers[60];
  ::std::future<int> consumer =
      ::std::async(&BlockingConsumerThread, &queue_, 60);

  // Make 50 producers, all using the same queue.
  for (int i = 0; i < 60; ++i) {
    producers[i] = ::std::thread(BlockingProducerThread, &queue_);
  }

  // Everything should sum to zero.
  EXPECT_EQ(0, consumer.get());

  // Join all the producers.
  for (int i = 0; i < 60; ++i) {
    producers[i].join();
  }
}

// Test that we can use the queue normally with a combination of blocking and
// non-blocking operations in the same thread.
TEST_F(MpscQueueTest, BlockingAndNonBlockingTest) {
  ::std::thread producer(AlternatingProducerThread, &queue_);
  ::std::future<int> consumer_ret =
      ::std::async(&AlternatingConsumerThread, &queue_, 1);

  // Wait for them both to finish.
  EXPECT_EQ(0, consumer_ret.get());
  producer.join();
}

}  // namespace testing
}  // namespace internal
}  // namespace gaia
