// NOTE: This file is not meant to be #included directly. Use mpsc_queue.h instead.

template <class T>
MpscQueue<T>::MpscQueue()
    : MpscQueue(nullptr) {}

template <class T>
MpscQueue<T>::MpscQueue(Pool *pool)
    : own_pool_(false), pool_(pool) {
  if (!pool_) {
    pool_ = new Pool(kPoolSize);
  }

  // Allocate the memory we need.
  queue_ = pool_->AllocateForType<RawQueue>();
  assert(queue_ != nullptr && "Out of memory?");

  queue_->write_length = 0;
  queue_->head_index = 0;

  for (int i = 0; i < kQueueCapacity; ++i) {
    queue_->array[i].valid = 0;
  }
}

template <class T>
MpscQueue<T>::MpscQueue(int queue_offset)
    : pool_(new Pool(kPoolSize)) {
  // Find the actual queue.
  queue_ = pool_->AtOffset<RawQueue *>(queue_offset);
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
bool MpscQueue<T>::Enqueue(const T &item) {
  // Increment the write length now, to keep other writers from writing off the
  // end.
  const int32_t old_length = ExchangeAdd(&(queue_->write_length), 1);
  if (old_length >= kQueueCapacity) {
    // The queue is full. Decrement again and return without doing anything.
    ExchangeAdd(&(queue_->write_length), -1);
    return false;
  }

  // Increment the write head to keep other writers from writing over this
  // space.
  int32_t old_head = ExchangeAdd(&(queue_->head_index), 1);
  // The ANDing is so we can easily make our indices wrap when they reach the
  // end of the physical array.
  constexpr uint32_t mask =
      ::std::numeric_limits<uint32_t>::max() >> (32 - kQueueCapacityShifts);
  BitwiseAnd(&(queue_->head_index), mask);
  // Technically, we need to do this for the index we're going to write to as
  // well, in case a bunch of increments got run before their respective
  // ANDings.
  old_head &= mask;

  Node *write_at = queue_->array + old_head;
  write_at->value = item;

  // Only now is it safe to alert readers that we have a new element.
  Exchange(&(write_at->valid), 1);

  return true;
}

template <class T>
bool MpscQueue<T>::DequeueNext(T *item) {
  // Check that the space we want to read is actually valid.
  Node *read_at = queue_->array + tail_index_;
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
  ExchangeAdd(&(queue_->write_length), -1);

  return true;
}
