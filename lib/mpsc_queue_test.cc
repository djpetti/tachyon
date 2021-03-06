#include <future>
#include <thread>

#include <gtest/gtest.h>

#include "constants.h"
#include "mpsc_queue.h"
#include "pool.h"

namespace tachyon {
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
    while (!queue->Enqueue(i))
      ;
  }
}

// Does the exact same thing as the function above, but uses blocking.
void BlockingProducerThread(MpscQueue<int> *queue) {
  for (int i = -3000; i <= 3000; ++i) {
    queue->EnqueueBlocking(i);
  }
}

// Does the same thing as the functions above, but alternates between blocking
// and non-blocking writes.
void AlternatingProducerThread(MpscQueue<int> *queue) {
  for (int i = -3000; i <= 3000; ++i) {
    if (i % 2) {
      queue->EnqueueBlocking(i);
    } else {
      while (!queue->Enqueue(i))
        ;
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
    while (!queue->DequeueNext(&compare))
      ;
    total += compare;
  }

  return total;
}

// Does the same thing as ConsumerThread, but peeks each item before dequeueing
// it.
int PeekingConsumerThread(MpscQueue<int> *queue, int num_producers) {
  int total = 0;
  for (int i = 0; i < 6001 * num_producers; ++i) {
    int compare;
    while (!queue->PeekNext(&compare));
    total += compare;

    // If it had something to peek, dequeuing should automatically succeed.
    if (!queue->DequeueNext(&compare)) {
      return -1;
    }
    total += compare;
  }

  return total;
}

// Does the exact same thing as the function above, but uses blocking.
int BlockingConsumerThread(MpscQueue<int> *queue, int num_producers) {
  int total = 0;
  for (int i = 0; i < 6001 * num_producers; ++i) {
    int compare;
    queue->DequeueNextBlocking(&compare);
    total += compare;
  }

  return total;
}

// Does the same thing as BlockingConsumerThread, but peeks each item before
// dequeueing it.
int BlockingPeekingConsumerThread(MpscQueue<int> *queue, int num_producers) {
  int total = 0;
  for (int i = 0; i < 6001 * num_producers; ++i) {
    int compare;
    queue->PeekNextBlocking(&compare);
    total += compare;

    // If it had something to peek, dequeuing should automatically succeed.
    if (!queue->DequeueNext(&compare)) {
      return -1;
    }
    total += compare;
  }

  return total;
}

// Does the same thing as the functions above, but alternates between blocking
// and non-blocking reads.
int AlternatingConsumerThread(MpscQueue<int> *queue, int num_producers) {
  int total = 0;
  for (int i = 0; i < 6001 * num_producers; ++i) {
    int compare;
    if (i % 2) {
      queue->DequeueNextBlocking(&compare);
    } else {
      while (!queue->DequeueNext(&compare))
        ;
    }
    total += compare;
  }

  return total;
}

}  // namespace

// Tests for the queue.
class MpscQueueTest : public ::testing::Test {
 protected:
  virtual void SetUp() {
    // Clear the pool for this process. This is where the queues store their
    // state, so clearing this ensures that tests don't affect each-other.
    Pool::GetPool()->Clear();

    // Create the queue.
    queue_ = MpscQueue<int>::Create(kQueueCapacity);
    ASSERT_NE(nullptr, queue_);
  }

  static void TearDownTestCase() {
    // Unlink SHM.
    ASSERT_TRUE(Pool::Unlink());
  }

  // The queue we are testing with.
  ::std::unique_ptr<MpscQueue<int>> queue_;
};

// Test that we can enqueue items properly.
TEST_F(MpscQueueTest, EnqueueTest) {
  // Fill up the entire queue.
  for (int i = 0; i < kQueueCapacity; ++i) {
    EXPECT_TRUE(queue_->Enqueue(i));
  }

  // Now it shouldn't let us do any more.
  EXPECT_FALSE(queue_->Enqueue(51));
}

// Test that we can dequeue items properly.
TEST_F(MpscQueueTest, DequeueTest) {
  // Put some items on the queue.
  for (int i = 0; i < 10; ++i) {
    ASSERT_TRUE(queue_->Enqueue(i));
  }

  // Try dequeing stuff.
  int on_queue;
  for (int i = 0; i < 10; ++i) {
    EXPECT_TRUE(queue_->DequeueNext(&on_queue));
    EXPECT_EQ(on_queue, i);
  }

  // There should be nothing else.
  EXPECT_FALSE(queue_->DequeueNext(&on_queue));
}

// Test that we can use the queue normally in a single-threaded case.
TEST_F(MpscQueueTest, SingleThreadTest) {
  int dequeue_counter = 0;
  int on_queue;
  for (int i = 0; i < 20; i += 2) {
    // Here, we'll enqueue two items and deque one.
    EXPECT_TRUE(queue_->Enqueue(i));
    EXPECT_TRUE(queue_->Enqueue(i + 1));

    EXPECT_TRUE(queue_->DequeueNext(&on_queue));
    EXPECT_EQ(dequeue_counter, on_queue);
    ++dequeue_counter;
  }

  // Now dequeue everything remaining.
  for (int i = 0; i < 10; ++i) {
    EXPECT_TRUE(queue_->DequeueNext(&on_queue));
    EXPECT_EQ(dequeue_counter, on_queue);
    ++dequeue_counter;
  }

  // There should be nothing left.
  EXPECT_FALSE(queue_->DequeueNext(&on_queue));
}

// Test that we can use the queue with structs.
TEST_F(MpscQueueTest, StructTest) {
  // Struct type for testing.
  struct TestStruct {
    int foo;
    char bar;
    double baz;
  };

  // Queue that passes structs.
  auto queue = MpscQueue<TestStruct>::Create(kQueueCapacity);

  int dequeue_counter = 0;
  TestStruct on_queue;
  const TestStruct base_item = {0, 'a', 42.0};
  for (int i = 0; i < 20; i += 2) {
    // Here, we'll enqueue two items and deque one.
    TestStruct item = base_item;
    item.foo = i;

    EXPECT_TRUE(queue->Enqueue(item));
    ++item.foo;
    EXPECT_TRUE(queue->Enqueue(item));

    TestStruct expected = base_item;
    expected.foo = dequeue_counter++;

    EXPECT_TRUE(queue->DequeueNext(&on_queue));

    // We don't have a good way of reliably comparing structs due to padding, so
    // we have to compare each member individually.
    EXPECT_EQ(expected.foo, on_queue.foo);
    EXPECT_EQ(expected.bar, on_queue.bar);
    EXPECT_EQ(expected.baz, on_queue.baz);
  }

  // Now dequeue everything remaining.
  for (int i = 0; i < 10; ++i) {
    TestStruct expected = base_item;
    expected.foo = dequeue_counter++;

    EXPECT_TRUE(queue->DequeueNext(&on_queue));

    EXPECT_EQ(expected.foo, on_queue.foo);
    EXPECT_EQ(expected.bar, on_queue.bar);
    EXPECT_EQ(expected.baz, on_queue.baz);
  }

  // There should be nothing left.
  EXPECT_FALSE(queue->DequeueNext(&on_queue));

  queue->FreeQueue();
}

// Test that we can use the queue normally with two threads.
TEST_F(MpscQueueTest, SpscTest) {
  ::std::thread producer(ProducerThread, queue_.get());
  ::std::future<int> consumer_ret =
      ::std::async(&ConsumerThread, queue_.get(), 1);

  // Wait for them both to finish.
  EXPECT_EQ(0, consumer_ret.get());
  producer.join();
}

// Test that we can use the queue normally with lots of threads.
TEST_F(MpscQueueTest, MpscTest) {
  ::std::thread producers[50];
  ::std::future<int> consumer = ::std::async(&ConsumerThread, queue_.get(), 50);

  // Make 50 producers, all using the same queue.
  for (int i = 0; i < 50; ++i) {
    producers[i] = ::std::thread(ProducerThread, queue_.get());
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
    queue_->EnqueueBlocking(i);
    queue_->EnqueueBlocking(i + 1);

    queue_->DequeueNextBlocking(&on_queue);
    EXPECT_EQ(dequeue_counter, on_queue);
    ++dequeue_counter;
  }

  // Now dequeue everything remaining.
  for (int i = 0; i < 10; ++i) {
    queue_->DequeueNextBlocking(&on_queue);
    EXPECT_EQ(dequeue_counter, on_queue);
    ++dequeue_counter;
  }

  // There should be nothing left.
  EXPECT_FALSE(queue_->DequeueNext(&on_queue));
}

// Test that we can use the queue normally with two threads and blocking.
TEST_F(MpscQueueTest, SpscBlockingTest) {
  ::std::thread producer(BlockingProducerThread, queue_.get());
  ::std::future<int> consumer_ret =
      ::std::async(&BlockingConsumerThread, queue_.get(), 1);

  // Wait for them both to finish.
  EXPECT_EQ(0, consumer_ret.get());
  producer.join();
}

// Test that we can use the queue normally with lots of threads and blocking.
TEST_F(MpscQueueTest, MpscBlockingTest) {
  ::std::thread producers[60];
  ::std::future<int> consumer =
      ::std::async(&BlockingConsumerThread, queue_.get(), 60);

  // Make 50 producers, all using the same queue.
  for (int i = 0; i < 60; ++i) {
    producers[i] = ::std::thread(BlockingProducerThread, queue_.get());
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
  ::std::thread producer(AlternatingProducerThread, queue_.get());
  ::std::future<int> consumer_ret =
      ::std::async(&AlternatingConsumerThread, queue_.get(), 1);

  // Wait for them both to finish.
  EXPECT_EQ(0, consumer_ret.get());
  producer.join();
}

// Tests that basic operations work with a smaller queue.
TEST_F(MpscQueueTest, SmallQueueSingleThreadTest) {
  // Create a new queue with a smaller size.
  auto queue = MpscQueue<int>::Create(1);

  int on_queue;
  for (int i = 0; i < 20; ++i) {
    // It should let us enqueue one, but not the second one.
    EXPECT_TRUE(queue->Enqueue(i));
    EXPECT_FALSE(queue->Enqueue(i + 1));

    EXPECT_TRUE(queue->DequeueNext(&on_queue));
    EXPECT_EQ(i, on_queue);
  }

  // There should be nothing left.
  EXPECT_FALSE(queue->DequeueNext(&on_queue));

  queue->FreeQueue();
}

// Test that we can use a small queue normally with two threads.
TEST_F(MpscQueueTest, SmallQueueSpscTest) {
  // Create a new queue with a smaller size.
  auto queue = MpscQueue<int>::Create(1);

  ::std::thread producer(ProducerThread, queue.get());
  ::std::future<int> consumer_ret =
      ::std::async(&ConsumerThread, queue.get(), 1);

  // Wait for them both to finish.
  EXPECT_EQ(0, consumer_ret.get());
  producer.join();
}

// Tests that basic blocking operations work with a smaller queue.
TEST_F(MpscQueueTest, SmallQueueSingleThreadBlockingTest) {
  // Create a new queue with a smaller size.
  auto queue = MpscQueue<int>::Create(1);

  int on_queue;
  for (int i = 0; i < 20; ++i) {
    queue->EnqueueBlocking(i);
    queue->DequeueNextBlocking(&on_queue);
    EXPECT_EQ(i, on_queue);
  }

  // There should be nothing left.
  EXPECT_FALSE(queue->DequeueNext(&on_queue));

  queue->FreeQueue();
}

// Test that we can use a small queue normally with two threads and blocking
// operations.
TEST_F(MpscQueueTest, SmallQueueSpscBlockingTest) {
  // Create a new queue with a smaller size.
  auto queue = MpscQueue<int>::Create(1);

  ::std::thread producer(BlockingProducerThread, queue.get());
  ::std::future<int> consumer_ret =
      ::std::async(&BlockingConsumerThread, queue.get(), 1);

  // Wait for them both to finish.
  EXPECT_EQ(0, consumer_ret.get());
  producer.join();
}

// Tests that Peek() operations work under the most basic of circumstances.
TEST_F(MpscQueueTest, SingleThreadPeekTest) {
  int on_queue;
  for (int i = 0; i < 20; ++i) {
    // Enqueue an item.
    EXPECT_TRUE(queue_->Enqueue(i));

    // Peek the item.
    EXPECT_TRUE(queue_->PeekNext(&on_queue));
    EXPECT_EQ(i, on_queue);
    // It should peek the same one again.
    EXPECT_TRUE(queue_->PeekNext(&on_queue));
    EXPECT_EQ(i, on_queue);

    // Dequeue the item.
    EXPECT_TRUE(queue_->DequeueNext(&on_queue));
    EXPECT_EQ(i, on_queue);
  }

  // There should be nothing left.
  EXPECT_FALSE(queue_->DequeueNext(&on_queue));
}

// Tests that Peek() operations work with multiple threads.
TEST_F(MpscQueueTest, MpscPeekTest) {
  // We only use 2 producers here for speed.
  ::std::thread producers[2];
  ::std::future<int> consumer =
      ::std::async(&PeekingConsumerThread, queue_.get(), 2);

  // Make 2 producers, both using the same queue.
  for (int i = 0; i < 2; ++i) {
    producers[i] = ::std::thread(ProducerThread, queue_.get());
  }

  // Everything should sum to zero.
  EXPECT_EQ(0, consumer.get());

  // Join all the producers.
  for (int i = 0; i < 2; ++i) {
    producers[i].join();
  }
}

// Tests that blocking Peek() operations work under the most basic of
// circumstances.
TEST_F(MpscQueueTest, SingleThreadBlockingPeekTest) {
  int on_queue;
  for (int i = 0; i < 20; ++i) {
    // Enqueue an item.
    EXPECT_TRUE(queue_->Enqueue(i));

    // Peek the item.
    queue_->PeekNextBlocking(&on_queue);
    EXPECT_EQ(i, on_queue);
    // It should peek the same one again.
    queue_->PeekNextBlocking(&on_queue);
    EXPECT_EQ(i, on_queue);

    // Dequeue the item.
    EXPECT_TRUE(queue_->DequeueNext(&on_queue));
    EXPECT_EQ(i, on_queue);
  }

  // There should be nothing left.
  EXPECT_FALSE(queue_->DequeueNext(&on_queue));
}

// Tests that blocking Peek() operations work with multiple threads.
TEST_F(MpscQueueTest, MpscBlockingPeekTest) {
  // We only use 2 producers here for speed.
  ::std::thread producers[2];
  ::std::future<int> consumer =
      ::std::async(&BlockingPeekingConsumerThread, queue_.get(), 2);

  // Make 2 producers, both using the same queue.
  for (int i = 0; i < 2; ++i) {
    producers[i] = ::std::thread(ProducerThread, queue_.get());
  }

  // Everything should sum to zero.
  EXPECT_EQ(0, consumer.get());

  // Join all the producers.
  for (int i = 0; i < 2; ++i) {
    producers[i].join();
  }
}

// Test that we can use the queue normally with two threads, blocking enqueues,
// and non-blocking dequeues. (This is meant to catch a specific bug in the
// implementation.)
TEST_F(MpscQueueTest, SpscBlockingWriteNormalReadTest) {
  // Push stuff onto the queue until it gets full. This will guarantee that the
  // producer thread will actually block at some point.
  while (queue_->Enqueue(1));

  ::std::thread producer(BlockingProducerThread, queue_.get());
  ::std::future<int> consumer_ret =
      ::std::async(&ConsumerThread, queue_.get(), 1);

  // Wait for them both to finish. It's not going to be zero, because we stuck
  // some extra stuff on there initially.
  int total = consumer_ret.get();

  // It also won't read everything because of the extra stuff, so do that now.
  int item;
  while (queue_->DequeueNext(&item)) {
    total += item;
  }

  EXPECT_EQ(kQueueCapacity, total);
  producer.join();
}

}  // namespace testing
}  // namespace tachyon
