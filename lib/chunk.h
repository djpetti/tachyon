#ifndef GAIA_LIB_CHUNK_H_
#define GAIA_LIB_CHUNK_H_

#include "lib/ipc/pool.h"
#include "lib/ipc/queue.h"

namespace gaia {

// A chunk is basically just a bit of memory that can be shared between
// different nodes in a Gaia cluster. It's sort of like a Blob in a Caffe, and
// functions as the principal abstraction in Gaia.
class Chunk {
 public:
  // Create an uninitialized chunk with a particular size.
  // Args:
  //  size: The size of the chunk. Note that this is not in bytes, but rather
  //  the number of floating-point values it can contain.
  explicit Chunk(int size);
  // Creates a chunk initialized from some initial data.
  // Args:
  //  data: An array of data to use for initialization.
  //  gradients: The gradients of that data.
  //  size: The number of items in the arrays.
  Chunk(const float *data, const float *gradients, int size);
  // Initializes a chunk from the serialized representation of another chunk.
  // Args:
  //  buffer: The buffer containing the serialized representation.
  explicit Chunk(const char *buffer);
  // Destructor.
  ~Chunk();

  // Define an assignment operator explicitly so this class
  // can be safely used with queues. These methods don't actually copy the
  // undelying data, so don't use them for normal things. (It's an odd version
  // of it anyway, so it probably won't work very well for normal uses.)
  // When using queues, be sure to use the Enqueue() and Dequeue() methods.
  // Note that the copy constructor is explicitly deleted; you shouldn't use it.
  Chunk(const Chunk &other) = delete;
  void operator=(const Chunk &other) volatile;

  // Sets the value of a chunk. It will copy the data specified into the chunk's
  // allocated storage. If the chunk is currently full of outside data set using
  // SetValueNoCopy(), it will automatically switch back to using its own
  // allocated storage, and the outside storage will not be overwritten.
  // Args:
  //  data: The array of data to set it to. It will read a number of elements
  //  from this array equal to the size of the chunk.
  //  gradients: The array of the gradients of this data.
  void SetValue(const float *data, const float *gradients);

  // Returns: A pointer to the data stored in this chunk.
  float *GetData() const;
  // Returns: A pointer to the gradients stored in this chunk.
  float *GetGradients() const;

  // Returns: The length of the char array necessary to serialize this chunk.
  int GetSerializedLength() const;
  // Creates a serialized version of the chunk which can be sent accross the
  // network.
  // Args:
  //  buffer: The buffer to write the serialized representation into. It should
  //  have at least the amount of free space returned by GetSerializedLength().
  void Serialize(char *buffer) const;

  // Put the chunk onto a queue. This is done in a special method so that the
  // contents of the chunk can explicitly be copied into shared memory for use
  // with the queue.
  // Args:
  //  queue: The queue we want to enqueue the chunk on.
  // Returns:
  //  True if it was successfully added, false if there wasn't space available.
  bool Enqueue(ipc::Queue<Chunk> *queue);
  // Same as the above, but blocks if there's no space available.
  void EnqueueBlocking(ipc::Queue<Chunk> *queue);

  // Get a chunk off a queue. (The data in the dequeued chunk will be copied
  // into this one.)
  // Args:
  //  queue: The queue we want to dequeue from.
  // Returns:
  //  True if it was successfully dequeued, false if the queue was empty.
  bool Dequeue(ipc::Queue<Chunk> *queue);
  // Same as the above, but blocks if the queue is empty.
  void DequeueBlocking(ipc::Queue<Chunk> *queue);

 private:
  // Does an enqueue operation.
  // Args:
  //  queue: The queue to enqueue onto.
  //  block: Whether to block or not.
  // Returns:
  //  True if it dequeued successfully, false if non-blocking was selected.
  bool DoEnqueue(ipc::Queue<Chunk> *queue, bool block);
  // Does a dequeue operation.
  // Args:
  //  queue: The queue to dequeue from.
  //  block: Whether to block or not.
  // Returns:
  //  True if it dequeued successfully, false if non-blocking was selected and
  //  the queue was empty.
  bool DoDequeue(ipc::Queue<Chunk> *queue, bool block);

  // The pool we are using for shared memory allocation.
  ipc::Pool *pool_;

  // Underlying data stored in the chunk.
  float *data_;
  // Underlying gradients stored in the chunk.
  float *gradients_;

  // Size of the two above arrays.
  int size_;
};

}  // namespace gaia

#endif  // GAIA_LIB_CHUNK_H_
