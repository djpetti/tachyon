#ifndef TACHYON_LIB_QUEUE_INTERFACE_H_
#define TACHYON_LIB_QUEUE_INTERFACE_H_

#include <stdint.h>

namespace tachyon {

// Defines a common interface for queue classes.
template <class T>
class QueueInterface {
 public:
  virtual ~QueueInterface() = default;

  // Adds a new element to the queue, without blocking. It is lock-free, and
  // stays in userspace.
  // Args:
  //  item: The item to add to the queue.
  // Returns:
  //  True if it succeeded in adding the item, false if the queue was
  //  full already.
  virtual bool Enqueue(const T &item) = 0;
  // Adds a new element to the queue, and blocks if there isn't space.
  // Args:
  //  item: The item to add to the queue.
  // Returns:
  //  True if writing the message succeeded, false if there were no consumers to
  //  write it to.
  virtual bool EnqueueBlocking(const T &item) = 0;

  // Removes an element from the queue, without blocking. It is lock-free, and
  // stays in userspace.
  // Args:
  //  item: A place to copy the item.
  // Returns:
  //  True if it succeeded in getting an item, false if the queue was empty
  //  already.
  virtual bool DequeueNext(T *item) = 0;
  // Removes an element from the queue, and blocks if the queue is empty.
  // Args:
  //  item: A place to copy the item.
  virtual void DequeueNextBlocking(T *item) = 0;

  // Gets the value of the next element to be removed from the queue, but does
  // not remove it. It is lock-free, and stays in userspace.
  // Args:
  //  item: A place to copy the item.
  // Returns:
  //  True if it succeeded in reading an item, false if the queue was empty
  //  already.
  virtual bool PeekNext(T *item) = 0;
  // Like PeekNext(), but blocks if the queue is empty until an element is
  // received.
  // Args:
  //  item: A place to copy the item.
  virtual void PeekNextBlocking(T *item) = 0;

  // Gets the offset in the pool of the shared memory portion of this queue.
  // Returns:
  //  The offset.
  virtual int GetOffset() const = 0;

  // Frees the underlying shared memory associated with this queue. Use this
  // method carefully, because once called, any futher operations on this
  // queue from any thread or process produce undefined results.
  virtual void FreeQueue() = 0;

  // Gets the number of consumers that are currently associated with this queue.
  // Returns:
  //  The current number of consumers.
  virtual uint32_t GetNumConsumers() const = 0;
};

}  // namespace tachyon

#endif  // TACHYON_LIB_QUEUE_INTERFACE_H_
