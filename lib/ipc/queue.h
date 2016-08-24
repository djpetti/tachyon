#ifndef GAIA_LIB_IPC_QUEUE_H_
#define GAIA_LIB_IPC_QUEUE_H_

#include <assert.h>
#include <stdint.h>

#include <memory>

#include "atomics.h"
#include "constants.h"
#include "mpsc_queue.h"
#include "pool.h"
#include "shared_hashmap.h"

namespace gaia {
namespace ipc {

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
// * Two different threads should never touch the same queue instance. If you
// want to give both threads access to the queue, make two different queue
// instances with the same queue_offset parameter.
template <class T>
class Queue {
 public:
  // Args:
  //  consumer: By default, items can be read from the queue using this
  //  instance. If, however, consumer is false, any dequeue operations will
  //  automatically segfault, or throw an assertion failure in debug mode.
  //  It is recommended that if this functionality is not needed, consumer
  //  be set to false, for efficiency's sake, since it stops it from writing
  //  to the corresponding subqueue. This option can also be used to ensure
  //  that enqueue operations don't fail because never-read subqueues
  //  are getting full.
  explicit Queue(bool consumer = true);
  // A similar contructor that fetches a queue stored at a particular location
  // in shared memory. Used internally by FetchQueue.
  // Args:
  //  queue_offset: The byte offset in the shared memory block of the underlying
  //  RawQueue object.
  //  consumer: See above for explanation.
  Queue(int queue_offset, bool consumer = true);
  ~Queue();

  // Adds a new element to the queue, without blocking. It is lock-free, and
  // stays in userspace.
  // Args:
  //  item: The item to add to the queue.
  // Returns:
  //  True if it succeeded in adding the item, false if the queue was
  //  full already.
  bool Enqueue(const T &item);
  // Adds a new element to the queue, and blocks if there isn't space.
  // Args:
  //  item: The item to add to the queue.
  // Returns:
  //  True if writing the message succeeded, false if there were no consumers to
  //  write it to.
  bool EnqueueBlocking(const T &item);

  // Removes an element from the queue, without blocking. It is lock-free, and
  // stays in userspace.
  // Args:
  //  item: A place to copy the item.
  // Returns:
  //  True if it succeeded in getting an item, false if the queue was empty
  //  already.
  bool DequeueNext(T *item);
  // Removes an element from the queue, and blocks if the queue is empty.
  // Args:
  //  item: A place to copy the item.
  void DequeueNextBlocking(T *item);

  // Gets the offset in the pool of the shared memory portion of this queue.
  // Returns:
  //  The offset.
  int GetOffset() const;

  // Frees the underlying shared memory associated with this queue. Use this
  // method carefully, because once called, any futher operations on this
  // queue from any thread or process produce undefined results.
  void FreeQueue();

  // Fetches a queue with the given name. If the queue does not exist, it
  // creates it. Otherwise, it fetches a new handle to the existing queue.
  // This particular method fetches a queue that can both produce and consume.
  // Args:
  //  name: The name of the queue to fetch.
  // Returns:
  //  The fetched queue.
  static ::std::unique_ptr<Queue<T>> FetchQueue(const char *name);
  // Same as the method above, but the queue that it fetches can only be used as
  // a producer.
  static ::std::unique_ptr<Queue<T>> FetchProducerQueue(const char *name);

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
    volatile uint32_t num_subqueues __attribute__((aligned(4)));
    // Offsets of all the subqueues in the pool, so we can easily find them.
    volatile Subqueue queue_offsets[kMaxConsumers];
  };

  // A hashmap that's in charge of mapping queue names to offsets. This is how
  // we implement fetching queues by name.
  static SharedHashmap<const char *, int> queue_names_;

  // Adds a new subqueue to this queue. This is needed whenever a new consumer
  // comes along.
  void AddSubqueue();
  // Checks for any new existing subqueues that were created by other processes,
  // and adds appropriate entries to our subqueues_ array.
  void IncorporateNewSubqueues();

  // Common back-end for FetchQueue and FetchProducerQueue.
  // Args:
  //  name: The name of the queue to fetch.
  //  consumer: Whether or not the queue should be a consumer queue.
  static ::std::unique_ptr<Queue<T>> DoFetchQueue(const char *name,
                                                  bool consumer);

  RawQueue *queue_;
  // This is the shared memory pool that we will use to construct queue objects.
  Pool *pool_;
  // The last value of queue_->num_subqueues we saw.
  uint32_t last_num_subqueues_ = 0;

  // This is the underlying array of MPSC queues that we use to implement this
  // MPMC queue.
  MpscQueue<T> *subqueues_[kMaxConsumers];
  // The particular subqueue that we read off of.
  MpscQueue<T> *my_subqueue_ = nullptr;
};

// Initialize the queue_names_ member.
template <class T>
SharedHashmap<const char *, int> Queue<T>::queue_names_(kNameMapOffset,
                                                        kNameMapSize);

#include "queue_impl.h"

}  // namespace ipc
}  // namespace gaia

#endif  // GAIA_LIB_IPC_QUEUE_H_
