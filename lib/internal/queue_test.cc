#include <stdio.h> // TEMP

#include <future>
#include <thread>

#include <gtest/gtest.h>

#include "constants.h"
#include "pool.h"
#include "queue.h"

namespace gaia {
namespace internal {
namespace testing {
namespace {

// A queue producer thread. It just runs in a loop and sticks a
// sequence on the queue.
// Args:
//  pool: The pool to use for making the queue.
//  offset: The SHM offset of the queue to use.
void ProducerThread(Pool *pool, int offset) {
  Queue<int> queue(pool, offset, false);

  for (int i = -3000; i <= 3000; ++i) {
    // We're doing non-blocking tests here, so we basically just spin around
    // until it works.
    while (!queue.Enqueue(i));
  }
}

// A queue consumer thread. It just runs in a loop and reads a sequence off the
// queue. The sequence is verified by checking that it sums to zero.
// Args:
//  pool: The pool to use for making the queue.
//  offset: The SHM offset of the queue to use.
//  num_producers: The number of producers we have.
int ConsumerThread(Pool *pool, int offset, int num_producers) {
  Queue<int> queue(pool, offset);

  int total = 0;
  for (int i = 0; i < 6001 * num_producers; ++i) {
    int compare;
    while (!queue.DequeueNext(&compare));
    total += compare;
  }

  return total;
}

}  // namespace

// Tests for the queue.
class QueueTest : public ::testing::Test {
 public:
  QueueTest() : pool_(kPoolSize, true), queue_(&pool_) {}

 protected:
  // Size of the pool to use for testing.
  static constexpr int kPoolSize = sizeof(int) * 1000;

  // The pool to use for testing.
  Pool pool_;
  // The queue we are testing with.
  Queue<int> queue_;
};

// Test that we can enqueue items properly.
TEST_F(QueueTest, EnqueueTest) {
  // Fill up the entire queue.
  for (int i = 0; i < kQueueCapacity; ++i) {
    EXPECT_TRUE(queue_.Enqueue(i));
  }

  // Now it shouldn't let us do any more.
  EXPECT_FALSE(queue_.Enqueue(51));
}

// Test that we can dequeue items properly.
TEST_F(QueueTest, DequeueTest) {
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
TEST_F(QueueTest, SingleThreadTest) {
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
TEST_F(QueueTest, SpscTest) {
  // Since Queue classes have some local state involved on both the producer and
  // consumer sides, we need to use separate queue instances. This is
  // complicated by the fact that mmap does not like mapping the same SHM
  // segment twice in the same process, so we have to explicitly tell it to use
  // the same pool instance.
  Queue<int> queue(&pool_, false);
  const int queue_offset = queue.GetOffset();

  ::std::thread producer(ProducerThread, &pool_, queue_offset);
  ::std::future<int> consumer_ret =
      ::std::async(&ConsumerThread, &pool_, queue_offset, 1);

  // Wait for them both to finish.
  EXPECT_EQ(0, consumer_ret.get());
  producer.join();
}

// Test that we can use the queue normally with lots of threads.
TEST_F(QueueTest, MpmcTest) {
  Queue<int> queue(&pool_, false);
  const int queue_offset = queue.GetOffset();

  ::std::thread producers[50];
  ::std::future<int> consumer = ::std::async(&ConsumerThread, &pool_,
                                             queue_offset, 50);

  // Make 50 producers, all using the same queue.
  for (int i = 0; i < 50; ++i) {
    producers[i] = ::std::thread(ProducerThread, &pool_, queue_offset);
  }

  // Everything should sum to zero.
  EXPECT_EQ(0, consumer.get());

  // Join all the producers.
  for (int i = 0; i < 50; ++i) {
    producers[i].join();
  }
}

}  // namespace testing
}  // namespace internal
}  // namespace gaia
