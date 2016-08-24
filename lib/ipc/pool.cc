#include "pool.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <mutex>

#include "macros.h"

namespace gaia {
namespace ipc {
namespace {

// Given a start offset and a size, it figures out the start and end indices and
// masks in the block allocation array of the specified memory region.
// Args:
//  offset: The offset of the region in bytes.
//  size: The size of the region in bytes.
//  start_index: The start index in block_allocation_.
//  start_mask: The start mask in block_allocation_.
//  end_index: The end index in block_allocation_.
//  end_mask: The end mask in block_allocation_.
void DefineSegment(int offset, int size, int *start_index, uint8_t *start_mask,
                   int *end_index, uint8_t *end_mask) {
  const int start_block = offset / kBlockSize;
  const int end_block = (offset + size - 1) / kBlockSize;

  *start_index = start_block >> 3;
  *start_mask = 1 << (start_block % 8);
  *end_index = end_block >> 3;
  *end_mask = 1 << (end_block % 8);
}

// Once flag to use for calling CreateSingletonPool.
::std::once_flag singleton_pool_once_flag;

}  // namespace

// Static members have to be initialized, or we have linker issues.
Pool *Pool::singleton_pool_ = nullptr;

Pool::Pool(int size) {
  // Allocate block of shared memory.
  int fd = shm_open(kShmName, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
  bool created = true;
  if (fd < 0 && errno == EEXIST) {
    // If it errors, assume this was because the SHM block already existed.
    created = false;
    fd = shm_open(kShmName, O_RDWR, S_IRUSR | S_IWUSR);
  }
  assert(fd >= 0 && "shm_open() failed.");

  if (created) {
    BuildNewPool(fd, size);
  } else {
    BuildExistingPool(fd, size);
  }

  // Initialize the mutex.
  MutexInit(&(header_->allocation_lock));
}

Pool::~Pool() {
  // Unmap our shared memory.
  munmap(header_, total_size_);
}

void Pool::BuildNewPool(int fd, int size) {
  int data_size, num_blocks, header_overhead;
  uint8_t *pool = MapShm(size, fd, &data_size, &num_blocks, &block_bytes_,
                         &header_overhead);

  // It turns out we actually have to make it the size we want.
  const int truncate_ret = ftruncate(fd, data_size + header_overhead);
  assert(truncate_ret >= 0 && "ftruncate() failed.");
  _UNUSED(truncate_ret);

  // Our pool header will start from the very beginning of the pool.
  header_ = reinterpret_cast<PoolHeader *>(pool);
  header_->size = data_size;
  header_->num_blocks = num_blocks;

  // The block allocation array starts right after the header.
  block_allocation_ = pool + sizeof(PoolHeader);
  // Nothing is allocated initially.
  Clear();

  // Mark where our actual data starts.
  data_ = pool + header_overhead;
}

void Pool::BuildExistingPool(int fd, int size) {
  int data_size, num_blocks, header_overhead;
  uint8_t *pool = MapShm(size, fd, &data_size, &num_blocks, &block_bytes_,
                         &header_overhead);

  header_ = reinterpret_cast<PoolHeader *>(pool);

  // Since our memory should already be initialized, we can just assume that
  // non-pointer members are valid. Pointer members, however, may not be
  // since we let mmap put it wherever it wanted.
  block_allocation_ = pool + sizeof(PoolHeader);
  // Mark where our actual data starts.
  data_ = pool + header_overhead;
}

uint8_t *Pool::Allocate(int size) {
  assert(size && "Allocating zero-length block?");

  // Grab the lock while we're doing stuff.
  MutexGrab(&(header_->allocation_lock));

  // We have to allocate in block units, so we can just divide this by the block
  // size to get the number of blocks.
  const int bytes = size;
  size /= kBlockSize;
  if (bytes % kBlockSize) {
    ++size;
  }

  // Find the smallest available memory block that still works.
  bool in_free_segment = false;
  bool set_segment = false;
  int segment_size = 0;
  int smallest_size = INT_MAX;
  uint8_t start_mask = 0;
  uint8_t start_mask_shifts = 0;
  int start_index = -1;
  // Stores information about segments.
  struct Segment {
    // Starting bit mask.
    uint8_t start_mask;
    // Ending bit mask.
    uint8_t end_mask;
    // Segment start byte index.
    int start_index;
    // Segment end byte index.
    int end_index;
    // The actual start byte of this segment in data_.
    int start_byte;
  };
  Segment segment = {0, 0, -1, -1, -1};
  Segment smallest_segment = segment;

  int saw_blocks = 0;

  for (int i = 0; i < block_bytes_; ++i) {
    uint8_t mask_shifts = 0;
    for (uint8_t mask = 1; mask != 0; mask <<= 1) {
      if ((block_allocation_[i] & mask) && in_free_segment) {
        // We reached the end of a free segment.
        in_free_segment = false;
        if (segment_size) {
          if (segment_size < smallest_size) {
            // We found a new smallest segment.
            smallest_size = segment_size;
            smallest_segment = segment;
          }
          segment_size = 0;
          set_segment = false;
        }
      }

      if (!(block_allocation_[i] & mask)) {
        // We reached the start of a free segment.
        if (!in_free_segment) {
          in_free_segment = true;
          start_mask = mask;
          start_index = i;
          start_mask_shifts = mask_shifts;
        }
        ++segment_size;
      }

      if (!set_segment && segment_size >= size) {
        // Save this as a possible segment for us to occupy.
        segment = {start_mask, mask, start_index, i,
                   ((start_index << 3) + start_mask_shifts) * kBlockSize};
        set_segment = true;
      }

      ++mask_shifts;
      ++saw_blocks;

      if (saw_blocks >= header_->num_blocks) {
        // We've seen all the blocks there are. We could get here because we have
        // to have complete bytes even if the number of blocks is not a multiple
        // of 8.
        break;
      }
    }
  }
  if (set_segment) {
    // We reached the end while still in a free segment.
    if (segment_size < smallest_size) {
      smallest_segment = segment;
      smallest_size = segment_size;
    }
  }

  if (smallest_size != INT_MAX) {
    // Set the segment as occupied.
    SetSegment(smallest_segment.start_index, smallest_segment.start_mask,
               smallest_segment.end_index, smallest_segment.end_mask, 1);

    MutexRelease(&(header_->allocation_lock));

    // Return the starting block.
    return data_ + smallest_segment.start_byte;
  }

  MutexRelease(&(header_->allocation_lock));

  // Not enough memory.
  return nullptr;
}

uint8_t *Pool::AllocateAt(int start_byte, int size) {
  // Check to make sure that this fits in the pool.
  assert(start_byte + size <= header_->size &&
         "Cannot allocate a segment this big.");

  // Grab the lock while we're doing stuff.
  MutexGrab(&(header_->allocation_lock));

  // Figure out the bits that we have to flip in block_allocation_.
  int start_index, end_index;
  uint8_t start_mask, end_mask;
  DefineSegment(start_byte, size, &start_index, &start_mask, &end_index,
                &end_mask);

  // First, make sure that the requested blocks are free.
  // Start with the beginning and end bytes, creating masks to we can check the
  // appropriate bits.
  const uint8_t start_free_mask = ~(start_mask - 1);
  const uint8_t end_free_mask = (end_mask << 1) - 1;
  if (block_allocation_[start_index] & start_free_mask) {
    MutexRelease(&(header_->allocation_lock));
    return nullptr;
  }
  if (block_allocation_[end_index] & end_free_mask) {
    MutexRelease(&(header_->allocation_lock));
    return nullptr;
  }

  // In the middle, we can check 64 bits at a time.
  int checking_index = start_index + 1;
  const int end_bound = end_index - (end_index % 8);
  while (checking_index < end_bound) {
    const uint64_t *checking_section = reinterpret_cast<uint64_t *>(
        block_allocation_ + start_index + checking_index);
    if (*checking_section) {
      MutexRelease(&(header_->allocation_lock));
      return nullptr;
    }

    checking_index += 8;
  }
  // Check the rest one byte at a time.
  while (checking_index < end_index) {
    if (block_allocation_[checking_index]) {
      MutexRelease(&(header_->allocation_lock));
      return nullptr;
    }

    ++checking_index;
  }

  // Set the memory as occupied.
  SetSegment(start_index, start_mask, end_index, end_mask, 1);

  MutexRelease(&(header_->allocation_lock));
  return data_ + start_byte;
}

void Pool::Free(uint8_t *block, int size) {
  // Grab the lock while we're doing stuff.
  MutexGrab(&(header_->allocation_lock));

  // Figure out the bits that we have to flip in block_allocation_.
  int start_index, end_index;
  uint8_t start_mask, end_mask;
  const int offset = GetOffset(block);
  DefineSegment(offset, size, &start_index, &start_mask, &end_index, &end_mask);

  // Set all the entries in the block allocation array for this segment to zero.
  SetSegment(start_index, start_mask, end_index, end_mask, 0);

  MutexRelease(&(header_->allocation_lock));
}

bool Pool::IsMemoryUsed(int offset) {
  // First, find the index of the block in the block allocation array.
  int start_index, end_index;
  uint8_t start_mask, end_mask;
  DefineSegment(offset, 1, &start_index, &start_mask, &end_index, &end_mask);

  MutexGrab(&(header_->allocation_lock));

  // Check if the block is being used.
  if (start_mask & block_allocation_[start_index]) {
    MutexRelease(&(header_->allocation_lock));
    return true;
  }

  MutexRelease(&(header_->allocation_lock));
  return false;
}

int Pool::get_size() const {
  return header_->size;
}

Pool *Pool::GetPool() {
  // Create the singleton pool.
  ::std::call_once(singleton_pool_once_flag, CreateSingletonPool, kPoolSize);

  return singleton_pool_;
}

bool Pool::Unlink() {
  return !shm_unlink(kShmName);
}

void Pool::SetSegment(int start_index, uint8_t start_mask, int end_index,
                      uint8_t end_mask, uint8_t value) {
  // We need to adapt our masks first so that we can actually use them to set
  // the first and last bytes.
  start_mask = ~(start_mask - 1);
  end_mask <<= 1;
  end_mask -= 1;
  if (start_index == end_index) {
    // An important edge case is if we're operating within the same byte, in
    // which case we don't want to set the whole byte.
    end_mask &= start_mask;
    start_mask = end_mask;
  }
  if (value) {
    block_allocation_[start_index] |= start_mask;
    block_allocation_[end_index] |= end_mask;
  } else {
    block_allocation_[start_index] &= ~start_mask;
    block_allocation_[end_index] &= ~end_mask;
  }

  // If there are no complete bytes in between, we're done.
  if (end_index - start_index < 2) {
    return;
  }

  // Fill in the middle.
  uint8_t fill;
  if (value) {
    fill = 0xFF;
  } else {
    fill = 0;
  }
  memset(block_allocation_ + start_index + 1, fill, end_index - start_index - 1);
}

void Pool::CalculateHeaderOverhead(int data_size, int num_blocks,
                                  int *block_bytes, int *header_overhead) {
  // If we use each byte as a bitfield, this is how many we'll need to have one
  // bit per block.
  *block_bytes = num_blocks >> 3;
  // If it wasn't divisible by 8, we need to add one more to account for the
  // overflow.
  if (num_blocks != (num_blocks & 0xF8)) {
    ++(*block_bytes);
  }

  // Calculate the overhead for the header.
  *header_overhead = sizeof(PoolHeader) + *block_bytes;
  // Align it to the block size.
  *header_overhead += (kBlockSize - (*header_overhead % kBlockSize));
}

uint8_t *Pool::MapShm(int size, int fd, int *data_size, int *num_blocks,
                      int *block_bytes, int *header_overhead) {
  // Calculate our actual data size, which has to be a multiple of our block size.
  *data_size = size + (kBlockSize - (size % kBlockSize));
  // Calculate total number of blocks.
  *num_blocks = *data_size / kBlockSize;

  CalculateHeaderOverhead(*data_size, *num_blocks, block_bytes, header_overhead);
  total_size_ = *data_size + *header_overhead;

  // Map into our address space.
  void *raw_pool = mmap(nullptr, total_size_, PROT_READ | PROT_WRITE,
                        MAP_SHARED | MAP_LOCKED, fd, 0);
  assert(raw_pool != MAP_FAILED && "mmap failed.");
  return static_cast<uint8_t *>(raw_pool);
}

int Pool::GetOffset(const void *shared_object) const {
  return reinterpret_cast<const uint8_t *>(shared_object) - data_;
}

void Pool::Clear() {
  // Effectively clearing the pool is as simple as zeroing the block allocation
  // array.
  memset(block_allocation_, 0, block_bytes_);
}

void Pool::CreateSingletonPool(int size) {
  // We never actually delete this.
  singleton_pool_ = new Pool(size);

  // Tell it to delete the pool when the process exits, for good measure.
  atexit(DeleteSingletonPool);
}

void Pool::DeleteSingletonPool() {
  delete singleton_pool_;
}

}  // namespace ipc
}  // namespace gaia
