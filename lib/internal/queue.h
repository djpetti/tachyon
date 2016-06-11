#ifndef GAIA_LIB_INTERNAL_QUEUE_H_
#define GAIA_LIB_INTERNAL_QUEUE_H_

#include "mutex.h"
#include "pool.h"

namespace gaia {
namespace internal {

// Should be pretty self-explanatory...
// At the low level, a Queue is basically just a vector with the nodes
// allocated in shared memory. Obviously, it's completely thread/process-safe.
// Template specialization is used to make Queues that can handle arbitrary
// types.
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
template <class T>
class Queue {
 public:
  Queue();
  // Constructor that makes a new queue but uses a pool that we pass in.
  // Args:
  //  pool: The pool to use.
  explicit Queue(Pool *pool);
  // A similar contructor that fetches a queue stored at a particular location
  // in shared memory. Used internally by FetchQueue.
  // Args:
  //  queue_offset: The byte offset in the shared memory block of the underlying
  //  RawQueue object..
  explicit Queue(int queue_offset);
  ~Queue();

  // Adds a new element to the queue, without blocking.
  // TODO (danielp): I'm pretty sure this can be done entirely atomically,
  // without mutexes.
  // Args:
  //  item: The item to add to the queue.
  // Returns:
  //  True if it succeeded in adding the item, false if the queue was full
  //  already.
  bool Enqueue(const T &item);

  // Removes an element from the queue, without blocking.
  // Args:
  //  item: A place to copy the item.
  // Returns:
  //  True if it succeeded in getting an item, false if the queue was empty
  //  already.
  bool DequeueNext(T *item);

 private:
  // How many items we want our queues to be able to hold.
  static constexpr int kQueueCapacity = 50;
  // Size to use when initializing the underlying pool.
  static constexpr int kPoolSize = 2000000;

  // This is the underlying structure that will be located in shared memory, and
  // contain everything that the queue needs to store in SHM. Multiple Queue
  // classes can share one of these, and they will be different "handles" into
  // the same queue.
  struct RawQueue {
    // The underlying array.
    T array[kQueueCapacity];
    // Total length of the queue.
    int length;
    // Current index of the head.
    int head_index;
    // Current index of the tail.
    int tail_index;
    // Mutex for protecting accesses to the head and tail, and length.
    Mutex mutex;
  };

  // Whether we own our pool or not.
  bool own_pool_ = true;

  RawQueue *queue_;
  // This is the shared memory pool that we will use to construct queue objects.
  Pool *pool_;
};

#include "queue_impl.h"

}  // namespace internal
}  // namespace gaia

#endif  // GAIA_LIB_INTERNAL_QUEUE_H_
