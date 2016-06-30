// NOTE: This file is not meant to be #included directly. Use mpsc_queue.h instead.

template <class T>
MpscQueue<T>::MpscQueue()
    : MpscQueue(nullptr) {}

template <class T>
MpscQueue<T>::MpscQueue(Pool *pool)
    : pool_(pool) {
  if (!pool_) {
    // Make our own pool.
    pool_ = new Pool(kPoolSize);
    own_pool_ = true;
  }

  // Allocate the shared memory we need.
  queue_ = pool_->AllocateForType<RawQueue>();
  assert(queue_ != nullptr && "Out of shared memory?");

  queue_->write_length = 0;
  queue_->head_index = 0;

  for (int i = 0; i < kQueueCapacity; ++i) {
    queue_->array[i].valid = 0;
  }
}

template <class T>
MpscQueue<T>::MpscQueue(int queue_offset)
    : MpscQueue(new Pool(kPoolSize), queue_offset) {}

template <class T>
MpscQueue<T>::MpscQueue(Pool *pool, int queue_offset)
    : pool_(pool) {
  // Find the actual queue.
  queue_ = pool_->AtOffset<RawQueue>(queue_offset);
}

template <class T>
MpscQueue<T>::~MpscQueue() {
  // If someone else passed in the pool, they own it, so we probably don't want
  // to delete it.
  if (own_pool_) {
    delete(pool_);
  }
}

template <class T>
bool MpscQueue<T>::Reserve() {
  // Increment the write length now, to keep other writers from writing off the
  // end.
  const int32_t old_length = ExchangeAdd(&(queue_->write_length), 1);
  Fence();
  if (old_length >= kQueueCapacity) {
    // The queue is full. Decrement again and return without doing anything.
    Decrement(&(queue_->write_length));
    return false;
  }

  return true;
}

template <class T>
void MpscQueue<T>::EnqueueAt(const T &item) {
  // Increment the write head to keep other writers from writing over this
  // space.
  int32_t old_head = ExchangeAdd(&(queue_->head_index), 1);
  Fence();
  // The ANDing is so we can easily make our indices wrap when they reach the
  // end of the physical array.
  constexpr uint32_t mask =
      ::std::numeric_limits<uint32_t>::max() >> (32 - kQueueCapacityShifts);
  BitwiseAnd(&(queue_->head_index), mask);
  // Technically, we need to do this for the index we're going to write to as
  // well, in case a bunch of increments got run before their respective
  // ANDings.
  old_head &= mask;

  volatile Node *write_at = queue_->array + old_head;
  write_at->value = item;

  // Only now is it safe to alert readers that we have a new element.
  Fence();
  Exchange(&(write_at->valid), 1);
}

template <class T>
void MpscQueue<T>::CancelReservation() {
  // Decrementing write_length lets other people write over this space again.
  Decrement(&(queue_->write_length));
}

template <class T>
bool MpscQueue<T>::Enqueue(const T &item) {
  if (!Reserve()) {
    return false;
  }
  EnqueueAt(item);

  return true;
}

template <class T>
bool MpscQueue<T>::DequeueNext(T *item) {
  // Check that the space we want to read is actually valid.
  volatile Node *read_at = queue_->array + tail_index_;
  if (!CompareExchange(&(read_at->valid), 1, 0)) {
    // This means the space was not valid to begin with, and we have nothing
    // left to read.
    return false;
  }

  *item = read_at->value;

  ++tail_index_;
  // The ANDing is so we can easily make our indices wrap when they reach the
  // end of the physical array.
  constexpr uint32_t mask =
      ::std::numeric_limits<uint32_t>::max() >> (32 - kQueueCapacityShifts);
  tail_index_ &= mask;

  // Only now is it safe to alert writers that we have one fewer element.
  Fence();
  ExchangeAdd(&(queue_->write_length), -1);

  return true;
}

template <class T>
int MpscQueue<T>::GetOffset() const {
  return pool_->GetOffset(queue_);
}
