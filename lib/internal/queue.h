#ifndef GAIA_LIB_INTERNAL_QUEUE_H_
#define GAIA_LIB_INTERNAL_QUEUE_H_

#include <assert.h>
#include <stdint.h>

#include "atomics.h"
#include "constants.h"
#include "mpsc_queue.h"
#include "pool.h"

namespace gaia {
namespace internal {

// This is a fully MPSC queue. All non-blocking operations on this queue are
// entirely lock-free and remain in userspace, aside from some of the
// initialization at the beginning.
//
// Non-blocking operations on this queue are, of course, suitable for realtime
// applications.
//
// A few tips to keep you out of trouble:
// * If you're using a queue to send stuff between processes, don't send
// anything that contains pointers through a queue, unless those pointers point
// to something in shared memory.
// * Queues will automatically size themselves appropriately for the objects
// they are sending. However, be careful about sending large objects, because
// you could run out of shared memory rather quickly.
// * In a similar vein, sending anything through a Queue is technically an O(n)
// operation on the size of the object, because it has to be copied into shared
// memory, and then copied out again at the other end.
// * This queue is slightly nonstandard in that all consumers will always read
// every single item on the queue. (I personally find this feature very useful.)
template <class T>
class Queue {
 public:
  // Args:
  //  consumer: By default, items can be read from the queue using this
  //  instance. If, however, consumer is false, any dequeue operations will
  //  automatically return false. It is recommended that if this functionality
  //  is not needed, consumer be set to false, for efficiency's sake, since it
  //  stops it from writing to the corresponding subqueue. This option can also
  //  be used to ensure that enqueue operations don't fail because never-read
  //  subqueues are getting full.
  Queue(bool consumer=true);
  // Constructor that makes a new queue but uses a pool that we pass in.
  // Args:
  //  pool: The pool to use.
  //  consumer: See above for explanation.
  explicit Queue(Pool *pool, bool consumer=true);
  // A similar contructor that fetches a queue stored at a particular location
  // in shared memory. Used internally by FetchQueue.
  // Args:
  //  queue_offset: The byte offset in the shared memory block of the underlying
  //  RawQueue object.
  //  consumer: See above for explanation.
  explicit Queue(int queue_offset, bool consumer=true);
  // Yet another constructor that combines the attributes of the two immediately
  // above it.
  // Args:
  //  pool: The pool to use.
  //  queue_offset: The byte offset in the shared memory block of the underlying
  //  RawQueue object.
  //  consumer: See above for explanation.
  Queue(Pool *pool, int queue_offset, bool consumer=true);
  ~Queue();

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

  // Gets the offset in the pool of the shared memory portion of this queue.
  // Returns:
  //  The offset.
  int GetOffset() const;

 private:
  // Represents a single item in the queue_offsets list.
  struct Subqueue {
    // The actual offset.
    volatile int32_t offset;
    // A flag indicating whether this subqueue is currently operational.
    volatile uint32_t valid __attribute__((aligned(4)));
  };

  // This is the underlying structure that will be located in shared memory, and
  // contain everything that the queue needs to store in SHM. Multiple Queue
  // classes can share one of these, and they will be different "handles" into
  // the same queue.
  struct RawQueue {
    // How many subqueues we currently have. (We don't necessarily use the whole
    // array.) The fancy alignment + volatile is because x86 guarantees that
    // accesses to a 4-byte aligned value will happen atomically.
    volatile int32_t num_subqueues __attribute__((aligned(4)));
    // Offsets of all the subqueues in the pool, so we can easily find them.
    volatile Subqueue queue_offsets[kMaxConsumers];
  };

  // Adds a new subqueue to this queue. This is needed whenever a new consumer
  // comes along.
  void AddSubqueue();
  // Checks for any new existing subqueues that were created by other processes,
  // and adds appropriate entries to our subqueues_ array.
  void IncorporateNewSubqueues();

  RawQueue *queue_;
  // This is the shared memory pool that we will use to construct queue objects.
  Pool *pool_;
  // Whether we own our pool or not.
  bool own_pool_ = false;
  // The last value of queue_->num_subqueues we saw.
  int32_t last_num_subqueues_ = 0;

  // This is the underlying array of MPSC queues that we use to implement this
  // MPMC queue.
  MpscQueue<T> *subqueues_[kMaxConsumers];
  // The particular subqueue that we read off of.
  MpscQueue<T> *my_subqueue_;
};

#include "queue_impl.h"

}  // namespace internal
}  // namespace gaia

#endif  // GAIA_LIB_INTERNAL_QUEUE_H_
