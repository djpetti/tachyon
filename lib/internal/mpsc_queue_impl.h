// NOTE: This file is not meant to be #included directly. Use mpsc_queue.h instead.

template <class T>
MpscQueue<T>::MpscQueue()
    : pool_(Pool::GetPool(kPoolSize)) {
  // Allocate the shared memory we need.
  queue_ = pool_->AllocateForType<RawQueue>();
  assert(queue_ != nullptr && "Out of shared memory?");

  queue_->write_length = 0;
  queue_->head_index = 0;

  for (int i = 0; i < kQueueCapacity; ++i) {
    queue_->array[i].valid = 0;
    queue_->array[i].write_waiters = 0;
  }
}

template <class T>
MpscQueue<T>::MpscQueue(int queue_offset)
    : pool_(Pool::GetPool(kPoolSize)) {
  // Find the actual queue.
  queue_ = pool_->AtOffset<RawQueue>(queue_offset);
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
  DoEnqueue(item, false);
}

template <class T>
void MpscQueue<T>::DoEnqueue(const T &item, bool will_block) {
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

  // Increment the number of people waiting. (This operates on only the first
  // 2 bytes.) We need to do this even if we're not blocking.
  volatile uint16_t *write_counter_ptr =
      reinterpret_cast<volatile uint16_t *>(&(write_at->write_waiters));
  const uint16_t my_wait_number = ExchangeAddWord(write_counter_ptr, 1);

  // Implement blocking if we need to.
  if (will_block) {
    DoWriteBlocking(write_at, my_wait_number);
  }

  write_at->value = item;

  // Only now is it safe to alert readers that we have a new element.
  Fence();
  const uint32_t old_valid = Exchange(&(write_at->valid), 1);
  if (old_valid == 2) {
    // If there was someone waiting for this to be valid, we need to wake them
    // up.
    const int woke_up = FutexWake(&(write_at->valid), 1);
    assert(woke_up <= 1 && "Woke up the wrong number of waiters?");
    _UNUSED(woke_up);
  }
}

template <class T>
void MpscQueue<T>::DoWriteBlocking(volatile Node *write_at,
                                   uint16_t my_wait_number) {
  // Ignore the MSB.
  my_wait_number &= 0x7FFF;

  // We're sort of implementing the deli algorithm here. We wait for
  // the wait counter to be equal to the woken counter.

  uint32_t write_waiters = write_at->write_waiters;
  uint16_t woken_counter = (write_waiters >> 16) & 0x7FFF;
  // If the MSBs of the two counters are the same, then we know that they have
  // both wrapped the same number of times, and we can use the standard logic
  // below. If, however, they are different, then one has wrapped once more
  // than the other, so we need to use the inverted logic instead.
  bool inverted = (write_waiters & (1 << 15)) != (write_waiters & (1 << 31));
  while ((!inverted && woken_counter < my_wait_number) ||
          (inverted && woken_counter > my_wait_number)) {
    FutexWait(&(write_at->write_waiters), write_waiters);

    write_waiters = write_at->write_waiters;
    woken_counter = (write_waiters >> 16) & 0x7FFF;
    inverted = (write_waiters & (1 << 15)) != (write_waiters & (1 << 31));
  }
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

  DoDequeue(item, read_at);

  // Only now is it safe to alert writers that we have one fewer element.
  Fence();
  Decrement(&(queue_->write_length));

  return true;
}

template <class T>
void MpscQueue<T>::DoDequeue(T *item, volatile Node *read_at) {
  *item = read_at->value;

  ++tail_index_;
  // The ANDing is so we can easily make our indices wrap when they reach the
  // end of the physical array.
  constexpr uint32_t mask =
      ::std::numeric_limits<uint32_t>::max() >> (32 - kQueueCapacityShifts);
  tail_index_ &= mask;

  // Increment the woken counter.
  volatile uint16_t *woken_counter =
      reinterpret_cast<volatile uint16_t *>(&(read_at->write_waiters)) + 1;
  IncrementWord(woken_counter);
}

template <class T>
void MpscQueue<T>::EnqueueBlocking(const T &item) {
  // Increment the write length now, to keep other writers from writing off the
  // end.
  int32_t length = ExchangeAdd(&(queue_->write_length), 1) + 1;
  Fence();

  // Now that we have a spot, we can just be lazy and let DoEnqueue() do the
  // work for us. If the queue is already full, we'll have it block for us too.
  DoEnqueue(item, length > kQueueCapacity);
}

template <class T>
void MpscQueue<T>::DequeueNextBlocking(T *item) {
  // Check that the space we want to read is actually valid.
  volatile Node *read_at = queue_->array + tail_index_;
  if (!CompareExchange(&(read_at->valid), 1, 0)) {
    // This means the space was not valid to begin with, and we have nothing
    // left to read. We indicate that we are waiting for something in this spot
    // by setting it's value to 2.
    if (CompareExchange(&(read_at->valid), 0, 2)) {
      while (read_at->valid == 2) {
        // Wait for it to become valid.
        FutexWait(&(read_at->valid), 2);
      }
    }
    // Since we only have one consumer, if we get here, we can be confident
    // that it's now valid. We now have to mark it as invalid before continuing,
    // though.
    Exchange(&(read_at->valid), 0);
  }
  assert(read_at->valid == 0 && "Reading from node not marked as invalid.");

  DoDequeue(item, read_at);

  // Only now is it safe to alert writers that we have one fewer element.
  Fence();
  const int32_t old_length = ExchangeAdd(&(queue_->write_length), -1);
  if (old_length > kQueueCapacity) {
    // There are people waiting that we need to wake up.
    // Wake all of them up. (One of them will actually continue.)
    Fence();
    FutexWake(&(read_at->write_waiters), ::std::numeric_limits<uint32_t>::max());
  }
}

template <class T>
int MpscQueue<T>::GetOffset() const {
  return pool_->GetOffset(queue_);
}

template <class T>
void MpscQueue<T>::FreeQueue() {
  pool_->FreeType<RawQueue>(queue_);
}
