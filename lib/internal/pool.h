#ifndef GAIA_LIB_INTERNAL_POOL_H_
#define GAIA_LIB_INTERNAL_POOL_H_

#include <assert.h>
#include <stdint.h>

#include "mutex.h"

namespace gaia {
namespace internal {

// Manages a pool of shared memory that queue messages are made from.
class Pool {
 public:
  // Creates a new pool, or links to an already existing one.
  // Args:
  //  size: The size in bytes of the pool to make. Important: If the pool
  //  already exists, this MUST BE THE SAME as the size used to create it,
  //  otherwise Allocate() might not give you blocks that are actually mapped in
  //  shared memory.
  //  clear: If this is true, it truncates the entire memory region before
  //  initializing it, otherwise, if the SHM segment already exists, the pool we
  //  be initialized with the data already in it.
  explicit Pool(int size, bool clear = false);
  ~Pool();

  // Gets a pointer to a block of memory from the pool.
  // Args:
  //  size: The size of the memory block.
  // Returns:
  //  A pointer to the start of the allocated block, or nullptr if there is no
  //  more memory left.
  uint8_t *Allocate(int size);
  // A helper function to allocate enough space to hold a specific type.
  // Returns:
  //  A pointer to the memory that will house that type.
  template <class T>
  T *AllocateForType() {
    uint8_t *raw = Allocate(sizeof(T));
    return reinterpret_cast<T *>(raw);
  }
  // A helper function to allocate enough space to hold an array of a specific
  // type.
  // Args:
  //  length: The length of the array.
  // Returns:
  //  A pointer to the memory that will house the array.
  template <class T>
  T *AllocateForArray(int length) {
    uint8_t *raw = Allocate(sizeof(T) * length);
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

  // Forcefully clears and reinitializes the shared memory block.
  // Returns:
  //  True if it succeeds, false otherwise.
  bool Clear();

  // Gets the block size for the pool. This is the minimum amount of data that
  // can be allocated at one time. (Requesting less data will allocate one block
  // regardless.)
  // Returns:
  //  The block size.
  static constexpr int get_block_size();

 private:
  // An instance of this struct actually lives in SHM and keeps track of
  // everything the class needs to know.
  struct PoolHeader {
    // The size of the pool in bytes.
    int size;
    // The number of blocks in the pool.
    int num_blocks;
    // The size of the block allocation array in bytes.
    int block_allocation_size;
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
};

}  // namespace internal
}  // namespace gaia

#endif  // GAIA_LIB_INTERNAL_POOL_H_
