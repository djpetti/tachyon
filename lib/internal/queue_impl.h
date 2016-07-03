// NOTE: This file is not meant to be #included directly. Use queue.h instead.

template <class T>
Queue<T>::Queue(bool consumer /*= true*/)
    : pool_(Pool::GetPool(kPoolSize)) {

  // Allocate the shared memory we need.
  // TODO (danielp): Add reference counting, so we can free shared memory when
  // we don't need it.
  queue_ = pool_->AllocateForType<RawQueue>();
  assert(queue_ != nullptr && "Out of shared memory?");

  queue_->num_subqueues = 0;

  // Initially, mark everything in the queue_offsets array as invalid.
  for (int i = 0; i < kMaxConsumers; ++i) {
    queue_->queue_offsets[i].valid = 0;
  }

  if (consumer) {
    // We're going to need at least one subqueue to begin with.
    AddSubqueue();
  }
}

template <class T>
Queue<T>::Queue(int queue_offset, bool consumer /*= true*/)
    : pool_(Pool::GetPool(kPoolSize)) {
  // Find the SHM portion of the queue.
  queue_ = pool_->AtOffset<RawQueue>(queue_offset);

  // This is the principal way in which we make get a new "handle" to the same
  // queue, so we're going to need to make another subqueue for us to read off
  // of.
  if (consumer) {
    AddSubqueue();
  }
  // Populate our array of subqueues.
  IncorporateNewSubqueues();
}

template <class T>
Queue<T>::~Queue() {
  // Get rid of subqueues.
  const int32_t num_subqueues = last_num_subqueues_;
  for (int i = 0; i < num_subqueues; ++i) {
    delete subqueues_[i];
  }
}

template <class T>
void Queue<T>::AddSubqueue() {
  // Increment this at the beginning so nobody can write over this spot.
  const int32_t queue_index = ExchangeAdd(&(queue_->num_subqueues), 1);
  Fence();

  ++last_num_subqueues_;

  // Create a new queue at that index.
  MpscQueue<T> *new_queue = new MpscQueue<T>();
  subqueues_[queue_index] = new_queue;
  my_subqueue_ = new_queue;

  // Record the offset so we can find it later.
  queue_->queue_offsets[queue_index].offset = new_queue->GetOffset();

  // Only once we're done messing with it can we make it valid.
  Fence();
  Exchange(&(queue_->queue_offsets[queue_index].valid), 1);
}

template <class T>
void Queue<T>::IncorporateNewSubqueues() {
  // It's okay to look at num_subqueues because we're guaranteed atomic access.
  // However, we copy it because we don't want it to change during the loop.
  const int32_t num_subqueues = queue_->num_subqueues;
  if (num_subqueues > last_num_subqueues_) {
    // We have new queues to add.
    for (int i = last_num_subqueues_; i < num_subqueues; ++i) {
      if (queue_->queue_offsets[i].valid) {
        const int32_t offset = queue_->queue_offsets[i].offset;
        subqueues_[last_num_subqueues_++] = new MpscQueue<T>(offset);
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
  for (int i = 0; i < last_num_subqueues_; ++i) {
    if (!subqueues_[i]->Reserve()) {
      // If they're not all going to work, we're going to cancel all our
      // reservations, not enqueue anything, and return false.
      for (int j = 0; j < i; ++j) {
        subqueues_[j]->CancelReservation();
      }
      return false;
    }
  }

  // If we get to here, we managed to reserve everything, so we're clear to
  // actually enqueue stuff.
  for (int i = 0; i < last_num_subqueues_; ++i) {
    subqueues_[i]->EnqueueAt(item);
  }

  return true;
}

template <class T>
bool Queue<T>::DequeueNext(T *item) {
  // Now, read from our designated subqueue.
  return my_subqueue_->DequeueNext(item);
}

template <class T>
int Queue<T>::GetOffset() const {
  return pool_->GetOffset(queue_);
}
