#ifndef TACHYON_LIB_IPC_MPSC_QUEUE_H_
#define TACHYON_LIB_IPC_MPSC_QUEUE_H_

#include <assert.h>
#include <stdint.h>

#include <limits>

#include "atomics.h"
#include "constants.h"
#include "macros.h"
#include "mutex.h"
#include "pool.h"

namespace tachyon {

// MPSC: "Multi-producer, single consumer."
// Should be pretty self-explanatory...
//
// At the low level, an MpscQueue is basically just a vector with the nodes
// allocated in shared memory. Obviously, it's completely thread/process-safe.
// Template specialization is used to make Queues that can handle arbitrary
// types.
//
// NOTE: This should never be used directly!!! It's only there as a helper class
// for standard Queues. This is because it's designed for only a single
// consumer.
//
// Non-blocking operations on this queue are, of course, lock free and suitable
// for realtime applications.
template <class T>
class MpscQueue {
 public:
  MpscQueue();
  // A similar contructor that fetches a queue stored at a particular location
  // in shared memory. Used internally by FetchQueue.
  // Args:
  //  queue_offset: The byte offset in the shared memory block of the underlying
  //  RawQueue object.
  explicit MpscQueue(int queue_offset);

  // Allows a user to "reserve" a place in the queue. Using this method will
  // save a space in the queue that nobody can write over, but which also can't
  // be read. If it succeeds, the next call to EnqueueAt() (from this thread)
  // will add an item in this spot. Otherwise, CancelReservation() can be used
  // to remove the reservation if you don't want to use it. This method does not
  // block.
  // IMPORTANT: If this method returns true, you MUST either call
  // CancelReservation or EnqueueAt afterwards!
  // Returns:
  //  True if it succeeds in reserving a spot, false if there is not space.
  bool Reserve();
  // Allows a user to enqueue and element at the spot they previously reserved.
  // IMPORTANT: The user MUST have successfully reserved a spot with Reserve(),
  // otherwise the behavior of this method is undefined.
  // Args:
  //  item: The item to add to the queue.
  void EnqueueAt(const T &item);
  // Allows a user to cancel a reservation previously made with Reserve().
  // IMPORTANT: The user MUST have successfully reserved a spot with Reserve(),
  // otherwise the behavior of this method will drop legitimate elements from
  // the queue.
  void CancelReservation();

  // Adds a new element to the queue, without blocking. It is lock-free, and
  // stays in userspace.
  // Args:
  //  item: The item to add to the queue.
  // Returns:
  //  True if it succeeded in adding the item, false if the queue was
  //  full already.
  bool Enqueue(const T &item);
  // Removes an element from the queue, without blocking. It is lock-free, and
  // stays in userspace.
  // Args:
  //  item: A place to copy the item.
  // Returns:
  //  True if it succeeded in getting an item, false if the queue was empty
  //  already.
  bool DequeueNext(T *item);

  // Adds a new element to the queue, and blocks if the queue is full.
  // With no contention, this method is lock-free.
  // Args:
  //  item: The item to add to the queue.
  void EnqueueBlocking(const T &item);
  // Removes an element from the queue, and blocks if the queue is empty.
  // With no contention, this method is lock-free.
  // Args:
  //  item: A place to copy the item.
  void DequeueNextBlocking(T *item);

  // Gets the offset of the shared part of the queue in the shared memory pool.
  // Returns:
  //  The offset.
  int GetOffset() const;

  // Frees the underlying shared memory that the queue uses. This is definitely
  // an "expert mode" method, because using it improperly can result in the
  // corruption of data that other processes are using. Only call it when you're
  // sure that this queue will no longer be used.
  void FreeQueue();

 private:
  // Represents an item in the queue.
  struct Node {
    // The actual item we want to store.
    volatile T value;
    // A flag denoting whether this node contains valid data. It is aligned
    // specially so that we can basically use it as a futex to implement
    // blocking reads.
    volatile uint32_t valid __attribute__((aligned(4)));
    // This is used to implement blocking writes. Bits 0 through 14 are a
    // counter of the number of people waiting for this location to become
    // writeable. Bits 16 through 30 are a counter of the number of people that
    // have been woken up. Both counters are only incremented and eventually
    // wrap. Normally, for any given waiter, if the latter counter is >= to the
    // value that the former counter was when we first incremented it, we know
    // that we are done waiting and can proceed. Because the counters wrap,
    // however, this metric is not always reliable. Bits 15 and 31 are compared,
    // therefore, and used to determine when to invert this logic. (The counters
    // are incremented like they are 16 bits, but only the first 15 bits are
    // read normally.)
    //
    // As long as you don't have more than 2^15 people waiting on the same
    // location at once, this system should work reliably. Also note that
    // because we have to wake everyone waiting on a particular space up every
    // time, having a massive number of writers blocked at once significantly
    // decreases the overall efficiency of the queue.
    //
    // TODO (danielp): Use the bitmask futex operations to increase the
    // efficiency with a large number of writers.
    // TODO (danielp): Use a 64-bit type here instead, which I think could make
    // some of our futex operations more efficient, since we can wait only on
    // the half that we care about.
    volatile uint32_t write_waiters __attribute__((aligned(4)));
  };

  // This is the underlying structure that will be located in shared memory, and
  // contain everything that the queue needs to store in SHM. Multiple Queue
  // classes can share one of these, and they will be different "handles" into
  // the same queue.
  struct RawQueue {
    // The underlying array.
    volatile Node array[kQueueCapacity];
    // Total length of the queue visible to writers. It is aligned specially
    // because we basically use it as a futex in order to implement blocking.
    volatile uint32_t write_length __attribute__((aligned(4)));
    // Current index of the head.
    volatile uint32_t head_index;
  };

  // Handles the implementation of blocking writes. It will block until the
  // requested node is available.
  // Args:
  //  write_at: A pointer to the node that we're trying to write to.
  //  my_wait_number: The value that we are waiting for the woken counter to
  //  reach before we continue.
  void DoWriteBlocking(volatile Node *write_at, uint16_t my_wait_number);
  // Actually writes an element to the queue. It assumes that a space was
  // already reserved by incrementing queue_->write_length.
  // Args:
  //  item: The item to write to the queue.
  //  can_block: Whether this write could block. This is mainly used as an
  //  optimization flag so we can forgo unnecessary computations for
  //  non-blocking writes.
  void DoEnqueue(const T &item, bool can_block);
  // Actually reads an element from the queue. It assumes that the space has
  // already been checked for validity. It also does not decrement write_length,
  // assuming that will be done afterwards.
  // Args:
  //  item: Where to write the item that was read from the queue.
  //  read_at: Node that we are reading from.
  void DoDequeue(T *item, volatile Node *read_at);

  // For consumers, we can get away with storing the tail index locally since we
  // only have one.
  uint32_t tail_index_ = 0;

  RawQueue *queue_;
  // This is the shared memory pool that we will use to construct queue objects.
  Pool *pool_;
};

#include "mpsc_queue_impl.h"

}  // namespace tachyon

#endif  // TACHYON_LIB_IPC_MPSC_QUEUE_H_
