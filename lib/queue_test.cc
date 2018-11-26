#include <future>
#include <thread>

#include <gtest/gtest.h>

#include "constants.h"
#include "pool.h"
#include "queue.h"

namespace tachyon {
namespace testing {
namespace {

// A queue producer thread. It just runs in a loop and sticks a
// sequence on the queue.
// Args:
//  offset: The SHM offset of the queue to use.
void ProducerThread(int offset) {
  Queue<int> queue(offset, false);

  for (int i = -3000; i <= 3000; ++i) {
    // We're doing non-blocking tests here, so we basically just spin around
    // until it works.
    while (!queue.Enqueue(i));
  }
}

// Same thing as the above function, but uses blocking writes.
void BlockingProducerThread(int offset) {
  Queue<int> queue(offset, false);

  for (int i = -3000; i <= 3000; ++i) {
    // Sometimes, producer threads will get kicked off before consumer threads.
    // In that case, if it returns false, we just want to spin until it works.
    while (!queue.EnqueueBlocking(i));
  }
}

// A queue consumer thread. It just runs in a loop and reads a sequence off the
// queue. The sequence is verified by checking that it sums to zero.
// Args:
//  offset: The SHM offset of the queue to use.
//  num_producers: The number of producers we have.
int ConsumerThread(int offset, int num_producers) {
  Queue<int> queue(offset);

  int total = 0;
  for (int i = 0; i < 6001 * num_producers; ++i) {
    int compare;
    while (!queue.DequeueNext(&compare));
    total += compare;
  }

  return total;
}

// Same thing as the above function, but uses blocking reads.
int BlockingConsumerThread(int offset, int num_producers) {
  Queue<int> queue(offset);

  int total = 0;
  for (int i = 0; i < 6001 * num_producers; ++i) {
    int compare;
    queue.DequeueNextBlocking(&compare);
    total += compare;
  }

  return total;
}

}  // namespace

// Tests for the queue.
class QueueTest : public ::testing::Test {
 protected:
  QueueTest() = default;

  virtual void TearDown() {
    // Free queue SHM.
    queue_.FreeQueue();
  }

  static void TearDownTestCase() {
    // Unlink SHM.
    ASSERT_TRUE(Pool::Unlink());
  }

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
  // consumer sides, we need to use separate queue instances.
  Queue<int> queue(false);
  const int queue_offset = queue.GetOffset();

  ::std::thread producer(ProducerThread, queue_offset);
  ::std::future<int> consumer_ret =
      ::std::async(&ConsumerThread, queue_offset, 1);

  // Wait for them both to finish.
  EXPECT_EQ(0, consumer_ret.get());
  producer.join();

  // Delete the new queue that we created.
  queue.FreeQueue();
}

// Test that we can use the queue normally with lots of threads.
TEST_F(QueueTest, MpmcTest) {
  Queue<int> queue(false);
  const int queue_offset = queue.GetOffset();

  ::std::thread producers[50];
  ::std::future<int> consumer =
      ::std::async(&ConsumerThread, queue_offset, 50);

  // Make 50 producers, all using the same queue.
  for (int i = 0; i < 50; ++i) {
    producers[i] = ::std::thread(ProducerThread, queue_offset);
  }

  // Everything should sum to zero.
  EXPECT_EQ(0, consumer.get());

  // Join all the producers.
  for (int i = 0; i < 50; ++i) {
    producers[i].join();
  }

  // Delete the new queue that we created.
  queue.FreeQueue();
}

// Test that we can use the queue normally in a single-threaded case with
// blocking.
TEST_F(QueueTest, SingleThreadBlockingTest) {
  int dequeue_counter = 0;
  int on_queue;
  for (int i = 0; i < 20; i += 2) {
    // Here, we'll enqueue two items and deque one.
    EXPECT_TRUE(queue_.EnqueueBlocking(i));
    EXPECT_TRUE(queue_.EnqueueBlocking(i + 1));

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
TEST_F(QueueTest, SpscBlockingTest) {
  // Since Queue classes have some local state involved on both the producer and
  // consumer sides, we need to use separate queue instances.
  Queue<int> queue(false);
  const int queue_offset = queue.GetOffset();

  ::std::thread producer(BlockingProducerThread, queue_offset);
  ::std::future<int> consumer_ret =
      ::std::async(&BlockingConsumerThread, queue_offset, 1);

  // Wait for them both to finish.
  EXPECT_EQ(0, consumer_ret.get());
  producer.join();

  // Delete the new queue that we created.
  queue.FreeQueue();
}

// Test that we can use the queue normally with lots of threads and blocking.
TEST_F(QueueTest, MpmcBlockingTest) {
  Queue<int> queue(false);
  const int queue_offset = queue.GetOffset();

  ::std::thread producers[50];
  ::std::future<int> consumer =
      ::std::async(&BlockingConsumerThread, queue_offset, 50);

  // Make 50 producers, all using the same queue.
  for (int i = 0; i < 50; ++i) {
    producers[i] = ::std::thread(BlockingProducerThread, queue_offset);
  }

  // Everything should sum to zero.
  EXPECT_EQ(0, consumer.get());

  // Join all the producers.
  for (int i = 0; i < 50; ++i) {
    producers[i].join();
  }

  // Delete the new queue that we created.
  queue.FreeQueue();
}

// Tests that fetching queues by name works.
TEST_F(QueueTest, FetchQueueTest) {
  auto queue1 = Queue<int>::FetchQueue("test_queue1");
  queue1->EnqueueBlocking(0);

  auto queue2 = Queue<int>::FetchQueue("test_queue2");
  queue2->EnqueueBlocking(1);

  // Now, it should have given us different queues, so we should be able to read
  // off the correct numbers.
  int result1, result2;
  queue1->DequeueNextBlocking(&result1);
  queue2->DequeueNextBlocking(&result2);
  EXPECT_EQ(0, result1);
  EXPECT_EQ(1, result2);
}

}  // namespace testing
}  // namespace tachyon
