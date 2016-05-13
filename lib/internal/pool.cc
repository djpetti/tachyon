#include "pool.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace gaia {
namespace internal {
namespace {

// Name of the shared memory block.
const char *kShmName = "/gaia_core";
// We allocate portions of SHM in blocks. This block size should be chosen to
// balance overhead with wasted space, and ideally the page size should be
// an integer multiple of this number.
constexpr int kBlockSize = 128;

}  // namespace

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
}

Pool::~Pool() {
  // Unmap our shared memory.
  munmap(header_, total_size_);
}

void Pool::BuildNewPool(int fd, int size) {
  int data_size, num_blocks, block_bytes, header_overhead;
  uint8_t *pool = MapShm(size, fd, &data_size, &num_blocks, &block_bytes,
                         &header_overhead);

  // It turns out we actually have to make it the size we want.
  assert(ftruncate(fd, data_size + header_overhead) >= 0 &&
         "ftruncate() failed.");

  // Our pool header will start from the very beginning of the pool.
  header_ = reinterpret_cast<PoolHeader *>(pool);
  header_->Size = data_size;
  header_->NumBlocks = num_blocks;

  // The block allocation array starts right after the header.
  header_->BlockAllocation = pool + sizeof(PoolHeader);
  // Right now, nothing is allocated, so that array should be completely
  // zeroed
  // out.
  memset(header_->BlockAllocation, 0, block_bytes);

  // Mark where our actual data starts.
  header_->Data = pool + header_overhead;

  header_->BlockAllocationSize = block_bytes;
}

void Pool::BuildExistingPool(int fd, int size) {
  int data_size, num_blocks, block_bytes, header_overhead;
  uint8_t *pool = MapShm(size, fd, &data_size, &num_blocks, &block_bytes,
                         &header_overhead);

  header_ = reinterpret_cast<PoolHeader *>(pool);

  // Since our memory should already be initialized, we can just assume that
  // non-pointer members are valid. Pointer members, however, may not be
  // since we let mmap put it wherever it wanted.
  header_->BlockAllocation = pool + sizeof(PoolHeader);
  // Mark where our actual data starts.
  header_->Data = pool + header_overhead;
}

uint8_t *Pool::Allocate(int size) {
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
  uint8_t start_mask, start_mask_shifts;
  int start_index;
  // Stores information about segments.
  struct Segment {
    // Starting bit mask.
    uint8_t StartMask;
    // Ending bit mask.
    uint8_t EndMask;
    // Segment start byte index.
    int StartIndex;
    // Segment end byte index.
    int EndIndex;
    // The actual start byte of this segment in header_->Data.
    int StartByte;
  } segment, smallest_segment;

  for (int i = 0; i < header_->BlockAllocationSize; ++i) {
    uint8_t mask_shifts = 0;
    for (uint8_t mask = 1; mask != 0; mask <<= 1) {
      if ((header_->BlockAllocation[i] & mask) && in_free_segment) {
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

      if (!(header_->BlockAllocation[i] & mask)) {
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
    }
  }
  if (set_segment) {
    // We reached the end while still in a free segment.
    smallest_segment = segment;
    smallest_size = segment_size;
  }

  if (smallest_size != INT_MAX) {
    // Set the segment as occupied.
    SetSegment(smallest_segment.StartIndex, smallest_segment.StartMask,
               smallest_segment.EndIndex, smallest_segment.EndMask, 1);

    // Return the starting block.
    return header_->Data + smallest_segment.StartByte;
  }

  // Not enough memory.
  return nullptr;
}

void Pool::Free(uint8_t *block, int size) {
  // Set all the entries in the block allocation array for this segment to zero.
  const int start_byte = block - header_->Data;
  const int start_index = start_byte / kBlockSize;
  const uint8_t start_mask = 1 << (start_byte % kBlockSize);
  const int end_index = (start_byte + size - 1) / kBlockSize;
  const uint8_t end_mask = 1 << ((start_byte + size - 1) % kBlockSize);

  SetSegment(start_index, start_mask, end_index, end_mask, 0);
}

void Pool::SetSegment(int start_index, uint8_t start_mask, int end_index,
                      uint8_t end_mask, uint8_t value) {
  // We need to adapt our masks first so that we can actually use them to set
  // the first and last bytes.
  start_mask = ~(start_mask - 1);
  end_mask += end_mask - 1;
  if (start_index == end_index) {
    // An important edge case is if we're operating within the same byte, in
    // which case we don't want to set the whole byte.
    end_mask &= start_mask;
    start_mask = end_mask;
  }
  if (value) {
    header_->BlockAllocation[start_index] |= start_mask;
    header_->BlockAllocation[end_index] |= end_mask;
  } else {
    header_->BlockAllocation[start_index] &= ~start_mask;
    header_->BlockAllocation[end_index] &= ~end_mask;
  }

  // Fill in the middle.
  uint8_t fill;
  if (value) {
    fill = 0xFF;
  } else {
    fill = 0;
  }
  for (int i = start_index + 1; i < end_index; ++i) {
    header_->BlockAllocation[i] = fill;
  }
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

}  // namespace internal
}  // namespace gaia
