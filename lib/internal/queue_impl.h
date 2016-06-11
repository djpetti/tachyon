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

  queue_->length = 0;
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
  MutexGrab(&(queue_->mutex));

  if (queue_->length == kQueueCapacity) {
    // Queue is full. There's nothing we can do here.
    MutexRelease(&(queue_->mutex));
    return false;
  }

  queue_->array[queue_->head_index] = item;

  ++queue_->head_index;
  if (queue_->head_index >= kQueueCapacity) {
    // It wrapped.
    queue_->head_index = 0;
  }
  ++queue_->length;

  MutexRelease(&(queue_->mutex));

  return true;
}

template <class T>
bool Queue<T>::DequeueNext(T *item) {
  MutexGrab(&(queue_->mutex));

  if (!queue_->length) {
    // Queue is empty.
    MutexRelease(&(queue_->mutex));
    return false;
  }

  *item = queue_->array[queue_->tail_index];

  ++queue_->tail_index;
  if (queue_->tail_index >= kQueueCapacity) {
    // It wrapped.
    queue_->tail_index = 0;
  }
  --queue_->length;

  MutexRelease(&(queue_->mutex));

  return true;
}
