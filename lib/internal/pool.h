#ifndef GAIA_LIB_INTERNAL_POOL_H_
#define GAIA_LIB_INTERNAL_POOL_H_

#include <assert.h>
#include <stdint.h>

#include "mutex.h"
#include "constants.h"

namespace gaia {
namespace internal {
// So we can friend tests...
namespace testing {
  class PoolTest_SharedTest_Test;
}

// Manages a pool of shared memory that queue messages are made from.
class Pool {
 public:
  // Gets a pointer to a block of memory from the pool.
  // Args:
  //  size: The size of the memory block.
  // Returns:
  //  A pointer to the start of the allocated block, or nullptr if there is no
  //  more memory left.
  uint8_t *Allocate(int size);
  // Reserves a block of memory in the pool at a particular offset. Success is
  // only guaranteed if all calls to this method come before any calls to
  // Allocate(), no requests overlap, and size is smaller than the pool size.
  // Args:
  //  start_byte: The byte offset in the pool where we want to allocate.
  //  size: The size of the memory block.
  // Returns:
  //  A pointer to the allocated block, or nullptr if the memory requested was
  //  not available.
  uint8_t *AllocateAt(int start_byte, int size);

  // A helper function to allocate enough space to hold a specific type.
  // Returns:
  //  A pointer to the memory that will store that type.
  template <class T>
  T *AllocateForType() {
    uint8_t *raw = Allocate(sizeof(T));
    return reinterpret_cast<T *>(raw);
  }
  // Same as the above, but uses AllocateAt() as the underlying allocator
  // instead of Allocate().
  // Args:
  //  start_byte: The byte offset in the pool where we want to allocate.
  // Returns:
  //  A pointer to the memory that will store that type.
  template <class T>
  T *AllocateForTypeAt(int start_byte) {
    uint8_t *raw = AllocateAt(start_byte, sizeof(T));
    return reinterpret_cast<T *>(raw);
  }
  // A helper function to allocate enough space to hold an array of a specific
  // type.
  // Args:
  //  length: The length of the array.
  // Returns:
  //  A pointer to the memory that will store the array.
  template <class T>
  T *AllocateForArray(int length) {
    uint8_t *raw = Allocate(sizeof(T) * length);
    return reinterpret_cast<T *>(raw);
  }
  // Also the same as the above, but uses AllocateAt() as the underlying
  // allocator instead of Allocate().
  // Args:
  //  start_byte: The byte offset in the pool where we want to allocate.
  //  size: length: The length of the array.
  template <class T>
  T *AllocateForArrayAt(int start_byte, int length) {
    uint8_t *raw = AllocateAt(start_byte, sizeof(T) * length);
    return reinterpret_cast<T *>(raw);
  }

  // Frees a block of allocated memory.
  // Args:
  //  block: A pointer to the start of the block.
  //  size: The size of the memory block.
  void Free(uint8_t *block, int size);
  // A helper function to free the underlying storage for a specific type.
  // Args:
  //  object: The object to free.
  template <class T>
  void FreeType(T *object) {
    uint8_t *raw = reinterpret_cast<uint8_t *>(object);
    Free(raw, sizeof(T));
  }
  // A helper function to free the underlying storage for an array of the
  // specific type.
  // Args:
  //  array: The array to free.
  //  length: The length of the array.
  template <class T>
  void FreeArray(T *array, int length) {
    uint8_t *raw = reinterpret_cast<uint8_t *>(array);
    Free(raw, sizeof(T) * length);
  }
  // Gets a valid pointer to the data located at a particular byte offset in the
  // shared memory block.
  // Args:
  //  offset: The offset to look at.
  // Returns:
  //  A pointer to the data at that offset.
  template <class T>
  T *AtOffset(int offset) {
    assert(offset < header_->size && "Out-of-bounds.");
    uint8_t *byte = data_ + offset;
    return reinterpret_cast<T *>(byte);
  }
  // Gets the offset in the pool of a pointer into pool memory.
  // Args:
  //  shared_object: The pointer to get the offset for.
  // Returns:
  //  The calculated offset.
  int GetOffset(const void *shared_object) const;

  // Forcefully clears the pool.
  // Returns:
  //  True if it succeeds, false otherwise.
  void Clear();

  // Gets the block size for the pool. This is the minimum amount of data that
  // can be allocated at one time. (Requesting less data will allocate one block
  // regardless.)
  // Returns:
  //  The block size.
  static constexpr int get_block_size() {
    return kBlockSize;
  }
  // Gets the total size of the size of the pool. This is the size of the shared
  // memory allocation with the size of the pool header subtracted.
  int get_size() const;

  // Either creates a new pool if none exists, or provides a pointer to the
  // existing pool for this process. This method is thread-safe. It uses the
  // pool size defined in constants.h.
  // Returns:
  //  A pointer to the pool.
  static Pool *GetPool();
  // Unlinks the shared memory segment and removes all the data stored in it.
  // You should be VERY, VERY CAREFUL with this method, because once it is
  // called, NOTHING ELSE in the entire application can use shared memory. It
  // should be called only when the application is exiting, and you are
  // absolutely sure that nothing will use shared memory again.
  // Returns:
  //  True if unlinking worked, false if it didn't.
  static bool Unlink();

 private:
  // Creates a new pool, or links to an already existing one.
  // This should never be called directly by the user, hence its privateness.
  // Use GetPool() instead.
  // Args:
  //  size: The size in bytes of the pool to make. Important: If the pool
  //  already exists, this MUST BE THE SAME as the size used to create it,
  //  otherwise Allocate() might not give you blocks that are actually mapped in
  //  shared memory.
  explicit Pool(int size);
  ~Pool();

  // An instance of this struct actually lives in SHM and keeps track of
  // everything the class needs to know. There should only ever be one of these
  // for any given application.
  struct PoolHeader {
    // The size of the pool in bytes.
    int size;
    // The number of blocks in the pool.
    int num_blocks;

    // Use this lock to protect allocations.
    Mutex allocation_lock;
  };

  // A pointer to our pool header.
  PoolHeader *header_;
  // Pointer to the block allocation array. This array keeps track of which
  // blocks are allocated and which aren't. It basically functions as a
  // bit field.
  uint8_t *block_allocation_;
  // Pointer to the start of the actual pool data.
  uint8_t *data_;

  // The total size of the memory allocation.
  int total_size_;
  // The total number of bytes we use for our block allocation array.
  int block_bytes_;

  // The pool instance that will be used for this process. (We can only mmap
  // stuff once per process.)
  static Pool *singleton_pool_;

  // Helper function that sets or unsets a segment of the block allocation
  // array.
  // Args:
  //  start_index: The byte index to the start of the segment.
  //  start_mask: The mask for the particular start bit within the byte referred
  //  to by start_index.
  //  end_index: The byte index to the end of the segment.
  //  end_mask: The mask for the last byte in the segment, within the byte
  //  referred to by end_index.
  //  value: Either zero or one. This is what we set everything in the segment
  //  to.
  void SetSegment(int start_index, uint8_t start_mask, int end_index,
                  uint8_t end_mask, uint8_t value);
  // Initializes everything from a newly-created pool of shared memory.
  // Args:
  //  fd: The file descriptor of the SHM region.
  //  size: The requested size of the pool.
  void BuildNewPool(int fd, int size);
  // Initializes everything in the pool header properly from existing shared
  // memory.
  // Args:
  //  fd: The file descriptor of the SHM region.
  //  size: The requested size of the pool.
  void BuildExistingPool(int fd, int size);
  // Calculate the total memory overhead for the header region.
  // Args:
  //  data_size: The size in bytes of the actual data region.
  //  num_blocks: The total number of blocks in the data region.
  //  block_bytes: If we use each byte as a bitfield, this is how many we'll
  //  need to have one bit per block.
  //  header_overhead: The total overhead of the header region.
  void CalculateHeaderOverhead(int data_size, int num_blocks, int *block_bytes,
                               int *header_overhead);
  // Shortcut for mapping an SHM segment into our address space.
  // Args:
  //  size: The requested size of the SHM segment.
  //  fd: The file descriptor that references the shared memory area.
  //  data_size: The actual size of our data region.
  //  num_blocks: The total number of blocks.
  //  block_bytes: The total number of bytes we need for our BlockAllocation
  //  bitfield.
  //  header_overhead: The total memory overhead of the header region.
  // Returns:
  //  uint8_t array containing the raw memory.
  uint8_t *MapShm(int size, int fd, int *data_size, int *num_blocks,
                  int *block_bytes, int *header_overhead);

  // This is so we can create the singleton pool for each process.
  // Args:
  //  size: The size of the pool.
  //  clear: Whether or not to clear the pool.
  static void CreateSingletonPool(int size);
  // Deletes the singleton pool when we're done with it.
  static void DeleteSingletonPool();
};

}  // namespace internal
}  // namespace gaia

#endif  // GAIA_LIB_INTERNAL_POOL_H_
