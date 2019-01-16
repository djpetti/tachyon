// NOTE: This file is not meant to be #included directly. Use queue.h instead.

template <class T>
Queue<T>::Queue(bool consumer /*= true*/)
    : pool_(Pool::GetPool()) {
  // Allocate the shared memory we need.
  queue_ = pool_->AllocateForType<RawQueue>();
  assert(queue_ != nullptr && "Out of shared memory?");

  queue_->num_subqueues = 0;

  // Allocate the subqueues array.
  subqueues_ = new MpscQueue<T> *[kMaxConsumers];
  assert(subqueues_ && "Failed to allocate subqueues.");

  // Initially, mark everything in the queue_offsets array as invalid and dead,
  // and mark the subqueues as invalid.
  for (int i = 0; i < kMaxConsumers; ++i) {
    queue_->queue_offsets[i].valid = 0;
    queue_->queue_offsets[i].dead = 1;
    subqueues_[i] = nullptr;
  }

  if (consumer) {
    // We're going to need at least one subqueue to begin with.
    MakeOwnSubqueue();
  }
}

template <class T>
Queue<T>::Queue(int queue_offset, bool consumer /*= true*/)
    : pool_(Pool::GetPool()) {
  // Find the SHM portion of the queue.
  queue_ = pool_->AtOffset<RawQueue>(queue_offset);

  // Allocate the subqueues array.
  subqueues_ = new MpscQueue<T> *[kMaxConsumers];
  assert(subqueues_ && "Failed to allocate subqueues.");

  // Mark all the subqueues as nonexistent.
  for (int i = 0; i < kMaxConsumers; ++i) {
    subqueues_[i] = nullptr;
  }

  // This is the principal way in which we make get a new "handle" to the same
  // queue, so we're going to need to make another subqueue for us to read off
  // of.
  if (consumer) {
    MakeOwnSubqueue();
  }
  // Populate our array of subqueues.
  IncorporateNewSubqueues();
}

template <class T>
Queue<T>::~Queue() {
  if (my_subqueue_) {
    // If this queue is a consumer, the subqueue that was created specifically for
    // it to read from will never be used again. First, mark it as invalid so
    // nobody will try to do anything with it again.
    Exchange(&(queue_->queue_offsets[my_subqueue_index_].valid), 0);
    Fence();

    // Remove the queue, freeing the SHM if necessary.
    RemoveSubqueue(my_subqueue_index_);
  }

  // Get rid of subqueues.
  // NOTE: This only deletes the local portion of the queue state. To delete the
  // shared portion, you must call FreeQueue().
  const uint32_t num_subqueues = last_num_subqueues_;
  for (uint32_t i = 0; i < num_subqueues; ++i) {
    delete subqueues_[i];
  }
  // Delete the array.
  delete[] subqueues_;
}

template <class T>
void Queue<T>::MakeOwnSubqueue() {
  // Look for any dead spaces that we can write over.
  uint32_t queue_index;
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

  // Create a new queue at that index.
  MpscQueue<T> *new_queue = new MpscQueue<T>();
  subqueues_[queue_index] = new_queue;
  my_subqueue_ = new_queue;
  my_subqueue_index_ = queue_index;

  // Record the offset so we can find it later.
  queue_->queue_offsets[queue_index].offset = new_queue->GetOffset();
  // Mark that we have one reference.
  queue_->queue_offsets[queue_index].num_references = 1;

  // Only once we're done messing with it can we make it valid.
  Fence();
  Exchange(&(queue_->queue_offsets[queue_index].valid), 1);

  // Mark that we have an additional subqueue.
  ++last_num_subqueues_;
  Increment(&(queue_->num_subqueues));
}

template <class T>
bool Queue<T>::AddSubqueue(uint32_t index) {
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
  const bool incremented =
      CompareExchange(&(queue_->queue_offsets[index].num_references), references,
                      references + 1);
  Fence();
  if (!incremented) {
    // The reference counter didn't have the expected value. It's not safe to
    // increment.
    return false;
  }

  // Go ahead and create the queue.
  const int32_t offset = queue_->queue_offsets[index].offset;
  subqueues_[index] = new MpscQueue<T>(offset);

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
    // Decrement the total number of subqueues.
    Decrement(&(queue_->num_subqueues));
  }

  // Delete the local portion of the queue.
  delete subqueues_[index];
  subqueues_[index] = nullptr;

  // Only now when we're done is it safe to mark this space as reusable.
  Fence();
  Exchange(&(queue_->queue_offsets[index].dead), 1);
}

template <class T>
void Queue<T>::IncorporateNewSubqueues() {
  const uint32_t num_subqueues = GetNumConsumers();

  if (num_subqueues != last_num_subqueues_) {
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

  // Since the subqueues support multiple producers, we can just write to all of
  // them in a pretty straightforward fashion.
  for (uint32_t i = 0; i < last_num_subqueues_; ++i) {
    if (!subqueues_[i]->Reserve()) {
      // If they're not all going to work, we're going to cancel all our
      // reservations, not enqueue anything, and return false.
      for (uint32_t j = 0; j < i; ++j) {
        subqueues_[j]->CancelReservation();
      }
      return false;
    }
  }

  // If we get to here, we managed to reserve everything, so we're clear to
  // actually enqueue stuff.
  for (uint32_t i = 0; i < last_num_subqueues_; ++i) {
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
  for (uint32_t i = 0; i < last_num_subqueues_; ++i) {
    subqueues_[i]->EnqueueBlocking(item);
  }

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
  for (uint32_t i = 0; i < last_num_subqueues_; ++i) {
    subqueues_[i]->FreeQueue();
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
                                                   bool consumer) {
  // First, see if a queue exists.
  int offset;
  if (queue_names_.Fetch(name, &offset)) {
    // We have a queue, so just make a new handle to it.
    Queue<T> *queue_handle = new Queue(offset, consumer);
    return ::std::unique_ptr<Queue<T>>(queue_handle);
  }

  // Create a new queue.
  Queue<T> *queue_handle = new Queue(consumer);
  // Save the offset.
  queue_names_.AddOrSet(name, queue_handle->GetOffset());

  return ::std::unique_ptr<Queue<T>>(queue_handle);
}

template <class T>
::std::unique_ptr<Queue<T>> Queue<T>::FetchQueue(const char *name) {
  return DoFetchQueue(name, true);
}

template <class T>
::std::unique_ptr<Queue<T>> Queue<T>::FetchProducerQueue(const char *name) {
  return DoFetchQueue(name, false);
}
