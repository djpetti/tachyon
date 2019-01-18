#ifndef TACHYON_LIB_QUEUE_H_
#define TACHYON_LIB_QUEUE_H_

#include <assert.h>
#include <stdint.h>

#include <memory>
#include <vector>

#include "atomics.h"
#include "constants.h"
#include "mpsc_queue.h"
#include "pool.h"
#include "queue_interface.h"
#include "shared_hashmap.h"

namespace tachyon {

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
class Queue : public QueueInterface<T> {
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
  virtual ~Queue();

  virtual bool Enqueue(const T &item);
  virtual bool EnqueueBlocking(const T &item);
  virtual bool DequeueNext(T *item);
  virtual void DequeueNextBlocking(T *item);

  virtual int GetOffset() const;

  virtual void FreeQueue();

  virtual uint32_t GetNumConsumers() const;

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
    volatile uint32_t valid;
    // A flag indicating that this subqueue will never be used again, and can be
    // overwritten.
    volatile uint32_t dead;
    // Number of references to this subqueue that are floating around.
    volatile uint32_t num_references;
    // Number of times this structure has been updated.
    volatile uint32_t num_updates;
  };

  // This is the underlying structure that will be located in shared memory, and
  // contain everything that the queue needs to store in SHM. Multiple Queue
  // classes can share one of these, and they will be different "handles" into
  // the same queue.
  struct RawQueue {
    // How many subqueues we currently have. (We don't necessarily use the whole
    // array.)
    volatile uint32_t num_subqueues;
    // Number of times we've either created or deleted a new subqueue.
    volatile uint32_t subqueue_updates;
    // Offsets of all the subqueues in the pool, so we can easily find them.
    volatile Subqueue queue_offsets[kMaxConsumers];
  };

  // A hashmap that's in charge of mapping queue names to offsets. This is how
  // we implement fetching queues by name.
  static SharedHashmap<const char *, int> queue_names_;

  // If this is a consumer queue, creates that subqueue that it will read from.
  void MakeOwnSubqueue();
  // Checks for any new existing subqueues that were created by other processes,
  // and adds appropriate entries to our subqueues_ array.
  void IncorporateNewSubqueues();

  // Adds a subqueue that exists in shared memory to this queue.
  // Args:
  //  index: The index in the queue_offsets array at which to add an entry for
  //         it.
  // Returns:
  //  True if adding the subqueue succeeded, false if the queue was deleted in
  //  another thread and can't be added.
  bool AddSubqueue(uint32_t index);
  // Removes a subqueue, possibly also deleting it from shared memory if this is
  // the last remaining reference to it.
  // Args:
  //  index: The index in the queue_offsets array at which the queue to remove
  //         is located.
  void RemoveSubqueue(uint32_t index);

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
  // The last value of queue_->subqueue_updates we saw.
  uint32_t last_subqueue_updates_ = 0;

  // This is the underlying array of MPSC queues that we use to implement this
  // MPMC queue.
  MpscQueue<T> **subqueues_;
  // Stored update counter for each subqueue.
  uint32_t *last_per_queue_updates_;
  // The particular subqueue that we read off of.
  MpscQueue<T> *my_subqueue_ = nullptr;
  // The index in queue_->queue_offsets of our subqueue.
  uint32_t my_subqueue_index_;

  // Stores indices of subqueues that are ready to be written to in order to
  // speed up the enqueue operation.
  ::std::vector<uint32_t> writable_subqueues_;
};

// Initialize the queue_names_ member.
template <class T>
SharedHashmap<const char *, int> Queue<T>::queue_names_(kNameMapOffset,
                                                        kNameMapSize);

#include "queue_impl.h"

}  // namespace tachyon

#endif  // TACHYON_LIB_QUEUE_H_
