// NOTE: This file is not meant to be #included directly. Use queue.h instead.

template <class T>
Queue<T>::Queue()
    : Queue(nullptr) {}

template <class T>
Queue<T>::Queue(Pool *pool)
    : own_pool_(false), pool_(pool) {
  if (!pool_) {
    pool_ = new Pool(kPoolSize);
  }

  // Allocate the memory we need.
  queue_ = pool_->AllocateForType<RawQueue>();
  assert(queue_ != nullptr && "Out of memory?");

  queue_->write_length = 0;
  queue_->read_length = 0;
  queue_->write_head = 0;
  queue_->read_head = 0;
  queue_->write_tail = 0;
  queue_->read_tail = 0;

  queue_->head_index = 0;
  queue_->tail_index = 0;

  MutexInit(&(queue_->mutex));
}

template <class T>
Queue<T>::Queue(int queue_offset)
    : pool_(new Pool(kPoolSize)) {
  // Find the actual queue.
  queue_ = pool_->AtOffset<RawQueue *>(queue_offset);
}

template <class T>
Queue<T>::~Queue() {
  // If someone else passed in the pool, they own it, so we probably don't want
  // to delete it.
  if (own_pool_) {
    delete(pool_);
  }
}

template <class T>
bool Queue<T>::Enqueue(const T &item) {
  // Increment the write length now, to keep other writers from writing off the
  // end.
  const int32_t old_length = ExchangeAdd(&(queue_->write_length), 1);
  if (old_length >= kQueueCapacity) {
    // The queue is full. Decrement again and return without doing anything.
    ExchangeAdd(&(queue_->write_length), -1);
    return false;
  }

  MutexGrab(&(queue_->mutex));

  queue_->array[queue_->head_index] = item;

  ++queue_->head_index;
  if (queue_->head_index >= kQueueCapacity) {
    // It wrapped.
    queue_->head_index = 0;
  }

  MutexRelease(&(queue_->mutex));

  // Only now is it safe to alert readers that we have a new element.
  ExchangeAdd(&(queue_->read_length), 1);

  return true;
}

template <class T>
bool Queue<T>::DequeueNext(T *item) {
  // Decrement the read length now, to keep other readers from reading off the
  // end.
  const int32_t old_length = ExchangeAdd(&(queue_->read_length), -1);
  if (old_length <= 0) {
    // The queue is empty. Increment again and return without doing anything.
    ExchangeAdd(&(queue_->read_length), 1);
    return false;
  }

  MutexGrab(&(queue_->mutex));

  *item = queue_->array[queue_->tail_index];

  ++queue_->tail_index;
  if (queue_->tail_index >= kQueueCapacity) {
    // It wrapped.
    queue_->tail_index = 0;
  }

  MutexRelease(&(queue_->mutex));

  // Only now is it safe to alert writers that we have one fewer element.
  ExchangeAdd(&(queue_->write_length), -1);

  return true;
}
