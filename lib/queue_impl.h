// NOTE: This file is not meant to be #included directly. Use queue.h instead.

template <class T>
Queue<T>::Queue() : pool_(Pool::GetPool()) {}

template <class T>
Queue<T>::~Queue() {
  if (my_subqueue_) {
    // If this queue is a consumer, the subqueue that was created specifically
    // for it to read from will never be used again. First, mark it as invalid
    // so nobody will try to do anything with it again.
    Exchange(&(queue_->queue_offsets[my_subqueue_index_].valid), 0);
    Fence();

    // Decrement the total number of subqueues.
    Decrement(&(queue_->num_subqueues));
    // Still an increment for the update counter, because this counts as an
    // update.
    Fence();
    Increment(&(queue_->subqueue_updates));
  }

  // Get rid of subqueues.
  for (uint32_t i = 0; i < kMaxConsumers; ++i) {
    if (subqueues_[i]) {
      // Delete any subqueues that we're still holding references to.
      RemoveSubqueue(i);
    }
  }
  // Delete the array.
  delete[] subqueues_;
}

template <class T>
void Queue<T>::DoCreate(bool consumer, uint32_t size) {
  // Allocate the shared memory we need.
  queue_ = pool_->AllocateForType<RawQueue>();
  assert(queue_ != nullptr && "Out of shared memory?");

  // Initialize the shared state.
  queue_->num_subqueues = 0;
  queue_->subqueue_size = size;
  queue_->subqueue_updates = 0;

  // Initially, mark everything in the queue_offsets array as invalid and dead.
  for (int i = 0; i < kMaxConsumers; ++i) {
    queue_->queue_offsets[i].valid = 0;
    queue_->queue_offsets[i].dead = 1;
  }

  InitializeLocalState(consumer);
}

template <class T>
void Queue<T>::DoLoad(bool consumer, uintptr_t queue_offset) {
  // Find the SHM portion of the queue.
  queue_ = pool_->AtOffset<RawQueue>(queue_offset);

  InitializeLocalState(consumer);
}

template <class T>
void Queue<T>::InitializeLocalState(bool consumer) {
  // Allocate the subqueues array.
  subqueues_ = new ::std::unique_ptr<MpscQueue<T>>[kMaxConsumers];
  assert(subqueues_ && "Failed to allocate subqueues.");

  // We'll go ahead and reserve as much space as we could possibly need here to
  // make the enqueue operation more realtime.
  writable_subqueues_.reserve(kMaxConsumers);

  // This is the principal way in which we make get a new "handle" to the same
  // queue, so we're going to need to make another subqueue for us to read off
  // of.
  if (consumer) {
    MakeOwnSubqueue();
  }
}

template <class T>
void Queue<T>::MakeOwnSubqueue() {
  // Look for any dead spaces that we can write over.
  uint32_t queue_index = kMaxConsumers;
  bool found_dead = false;
  for (uint32_t i = 0; i < kMaxConsumers; ++i) {
    // Read the dead flag. If the space is available, grab it now.
    const bool was_dead =
        CompareExchange(&(queue_->queue_offsets[i].dead), 1, 0);
    Fence();

    if (was_dead) {
      // We can overwrite this space.
      queue_index = i;
      found_dead = true;
      break;
    }
  }

  // If there were no new slots available, this constitutes a serious error.
  assert(found_dead && "Exceeded maximum number of consumers.");
  _UNUSED(found_dead);

  // Create a new queue at that index.
  auto new_queue = MpscQueue<T>::Create(queue_->subqueue_size);
  // TODO (danielp): Error handling for case when queue creation fails.
  subqueues_[queue_index] = ::std::move(new_queue);
  my_subqueue_ = subqueues_[queue_index].get();
  my_subqueue_index_ = queue_index;

  // Record the offset so we can find it later.
  queue_->queue_offsets[queue_index].offset = my_subqueue_->GetOffset();
  // Mark that we have one reference.
  queue_->queue_offsets[queue_index].num_references = 1;

  // Only once we're done messing with it can we make it valid.
  Fence();
  Exchange(&(queue_->queue_offsets[queue_index].valid), 1);

  // Mark that we have an additional subqueue.
  ++last_num_subqueues_;
  ++last_subqueue_updates_;
  Fence();
  Increment(&(queue_->subqueue_updates));
  Fence();
  Increment(&(queue_->num_subqueues));
}

template <class T>
bool Queue<T>::AddSubqueue(uint32_t index) {
  bool incremented = false;
  do {
    // Snapshot the value of the reference counter.
    const uint32_t references =
        ExchangeAdd(&(queue_->queue_offsets[index].num_references), 0);
    Fence();

    if (references == 0) {
      // The queue was already freed in another thread. Adding it would be
      // invalid.
      return false;
    }

    // Now, try to safely increment the counter.
    incremented =
        CompareExchange(&(queue_->queue_offsets[index].num_references),
                        references, references + 1);
    Fence();

    // This might fail if the reference counter does not have the expected
    // value, in which case it's not safe to increment and we need to try again.
  } while (!incremented);

  // Go ahead and create the queue.
  const int32_t offset = queue_->queue_offsets[index].offset;
  subqueues_[index] = MpscQueue<T>::Load(offset);

  return true;
}

template <class T>
void Queue<T>::RemoveSubqueue(uint32_t index) {
  // Decrement the reference counter.
  const uint32_t references =
      ExchangeAdd(&(queue_->queue_offsets[index].num_references), -1);
  Fence();

  if (references == 1) {
    // The reference counter just hit zero, which means we need to free the SHM.
    subqueues_[index]->FreeQueue();

    // Only now when we're done is it safe to mark this space as reusable.
    Fence();
    Exchange(&(queue_->queue_offsets[index].dead), 1);
  }

  // Delete the local portion of the queue.
  subqueues_[index].reset();
}

template <class T>
void Queue<T>::IncorporateNewSubqueues() {
  // We use a sneaky ExchangeAdd here in order to force atomic access of the
  // num_subqueues variable.
  const uint32_t subqueue_updates = ExchangeAdd(&(queue_->subqueue_updates), 0);
  Fence();

  if (subqueue_updates != last_subqueue_updates_) {
    // We have queues to add or remove.
    for (uint32_t i = 0; i < kMaxConsumers; ++i) {
      // Another sneaky atomic access for valid...
      const uint32_t valid = ExchangeAdd(&(queue_->queue_offsets[i].valid), 0);

      if (valid && !subqueues_[i] && AddSubqueue(i)) {
        // This subqueue is now valid, but not reflected in our subqueues array.
        ++last_num_subqueues_;
      } else if (!valid && subqueues_[i]) {
        // This subqueue is now invalid, but not reflected in our subqueues
        // array.
        RemoveSubqueue(i);
        --last_num_subqueues_;
      }
    }

    last_subqueue_updates_ = subqueue_updates;
  }
}

template <class T>
bool Queue<T>::Enqueue(const T &item) {
  // First, add any new subqueues that might have been created since we last ran
  // this.
  IncorporateNewSubqueues();

  // If we have no consumers, we'd basically just be sending this message out
  // into the void.
  if (!last_num_subqueues_) {
    return false;
  }

  writable_subqueues_.clear();

  // Since the subqueues support multiple producers, we can just write to all of
  // them in a pretty straightforward fashion.
  for (uint32_t i = 0; i < kMaxConsumers; ++i) {
    if (!subqueues_[i]) {
      // No queue here.
      continue;
    }

    if (!subqueues_[i]->Reserve()) {
      // If they're not all going to work, we're going to cancel all our
      // reservations, not enqueue anything, and return false.
      for (auto j : writable_subqueues_) {
        subqueues_[j]->CancelReservation();
      }
      return false;
    }

    writable_subqueues_.push_back(i);
    if (writable_subqueues_.size() == last_num_subqueues_) {
      // We've found all the subqueues that exist, so there's no point in
      // continuing.
      break;
    }
  }
  // It should be impossible for subqueues_ to be modified without our
  // knowledge.
  assert(writable_subqueues_.size() == last_num_subqueues_);

  // If we get to here, we managed to reserve everything, so we're clear to
  // actually enqueue stuff.
  for (auto i : writable_subqueues_) {
    subqueues_[i]->EnqueueAt(item);
  }

  return true;
}

template <class T>
bool Queue<T>::EnqueueBlocking(const T &item) {
  // First, add any new subqueues that might have been created since we last ran
  // this.
  IncorporateNewSubqueues();

  // If we have no consumers, we'd basically just be sending this message out
  // into the void.
  if (!last_num_subqueues_) {
    return false;
  }

  // Since the subqueues support multiple producers, we can just write to all of
  // them in a pretty straightforward fashion.
  uint32_t num_written = 0;
  for (uint32_t i = 0; i < kMaxConsumers; ++i) {
    if (!subqueues_[i]) {
      // No queue here.
      continue;
    }

    subqueues_[i]->EnqueueBlocking(item);

    if (++num_written == last_num_subqueues_) {
      // We've found all the subqueues that exist, so there's no point in
      // continuing.
      break;
    }
  }
  // It should be impossible for subqueues_ to be modified without our
  // knowledge.
  assert(num_written == last_num_subqueues_);

  return true;
}

template <class T>
bool Queue<T>::DequeueNext(T *item) {
  // Now, read from our designated subqueue.
  assert(my_subqueue_ && "This queue is not configured as a consumer!");
  return my_subqueue_->DequeueNext(item);
}

template <class T>
void Queue<T>::DequeueNextBlocking(T *item) {
  // Now, read from our designated subqueue.
  assert(my_subqueue_ && "This queue is not configured as a consumer!");
  my_subqueue_->DequeueNextBlocking(item);
}

template <class T>
int Queue<T>::GetOffset() const {
  return pool_->GetOffset(queue_);
}

template <class T>
void Queue<T>::FreeQueue() {
  // We want to make sure we free everything, so we need all the subqueues
  // locally.
  IncorporateNewSubqueues();

  // Free shared memory for the underlying subqueues.
  for (uint32_t i = 0; i < kMaxConsumers; ++i) {
    if (subqueues_[i]) {
      subqueues_[i]->FreeQueue();
    }
  }

  // Now free our underlying shared memory.
  pool_->FreeType<RawQueue>(queue_);
}

template <class T>
uint32_t Queue<T>::GetNumConsumers() const {
  // We use a sneaky ExchangeAdd here in order to force atomic access of the
  // num_subqueues variable.
  return ExchangeAdd(&(queue_->num_subqueues), 0);
}

template <class T>
::std::unique_ptr<Queue<T>> Queue<T>::DoFetchQueue(const char *name,
                                                   bool consumer,
                                                   uint32_t size) {
  // First, see if a queue exists.
  int offset;
  if (queue_names_.Fetch(name, &offset)) {
    // We have a queue, so just make a new handle to it.
    return Queue<T>::Load(consumer, offset);
  }

  // Create a new queue.
  auto queue_handle = Queue<T>::Create(consumer, size);
  // Save the offset.
  queue_names_.AddOrSet(name, queue_handle->GetOffset());

  return queue_handle;
}

template <class T>
::std::unique_ptr<Queue<T>> Queue<T>::Create(bool consumer, uint32_t size) {
  // Create new queue.
  Queue<T> *raw_queue = new Queue<T>();
  auto queue = ::std::unique_ptr<Queue<T>>(raw_queue);

  queue->DoCreate(consumer, size);

  return queue;
}

template <class T>
::std::unique_ptr<Queue<T>> Queue<T>::Load(bool consumer, uintptr_t offset) {
  // Create new queue.
  Queue<T> *raw_queue = new Queue<T>();
  auto queue = ::std::unique_ptr<Queue<T>>(raw_queue);

  queue->DoLoad(consumer, offset);

  return queue;
}

template <class T>
::std::unique_ptr<Queue<T>> Queue<T>::FetchQueue(const char *name) {
  // Use default size.
  return DoFetchQueue(name, true, kQueueCapacity);
}

template <class T>
::std::unique_ptr<Queue<T>> Queue<T>::FetchProducerQueue(const char *name) {
  return DoFetchQueue(name, false, kQueueCapacity);
}

template <class T>
::std::unique_ptr<Queue<T>> Queue<T>::FetchSizedQueue(const char *name,
                                                      uint32_t size) {
  // Use default size.
  return DoFetchQueue(name, true, size);
}

template <class T>
::std::unique_ptr<Queue<T>> Queue<T>::FetchSizedProducerQueue(const char *name,
                                                              uint32_t size) {
  return DoFetchQueue(name, false, size);
}
