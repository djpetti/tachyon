// NOTE: This file is not meant to be #included directly. Use shared_hashmap.h
// instead.

namespace internal {

// Here's a class whose sole purpose is to implement string-specific
// functionality.
template <class T>
class StringSpecific {
 public:
  // Converts a key, and returns what to set the bucket's Key value as.
  // Args:
  //  key: The key we want to set.
  static T ConvertKey(const T &key) {
    // If it's trivially copyable, just use our normal key.
    return key;
  }

  // Compares two keys.
  // Args:
  //  key1: The first key to compare.
  //  key2: The second key to compare.
  // Returns:
  //  True if the keys are the same, false if they aren't.
  static bool CompareKeys(const T &key1, const T &key2) {
    return key1 == key2;
  }

  // Hashes a key.
  // Args:
  //  The key to hash.
  // Returns:
  //  The hashed key.
  static ::std::size_t HashKey(const T &key) {
    return ::std::hash<T>()(key);
  }
};

// Explicit specialization of ConvertKey for strings.
// This works the same way as the normal version, except it properly copies the
// string into shared memory.
template <>
const char *StringSpecific<const char *>::ConvertKey(const char *const &key) {
  const int key_length = strlen(key) + 1; // Include \0.

  Pool *pool = Pool::GetPool();
  char *shared_key = reinterpret_cast<char *>(pool->Allocate(key_length));
  assert(shared_key && "Allocating SHM failed unexpectedly.");
  memcpy(shared_key, key, key_length);

  return shared_key;
}

// Explicit specialization of CompareKeys for strings.
// This works the same way as the normal version, except that it compares the
// strings character-by-character instead of merely comparing pointers.
template <>
bool StringSpecific<const char *>::CompareKeys(const char *const &key1,
                                               const char *const &key2) {
  if (!key1 || !key2) {
    // A special case is if one of them is null.
    return key1 == key2;
  }

  return !strcmp(key1, key2);
}

// Explicit specialization of HashKey for strings.
// std::hash can only work with std::strings, so we need to convert it to one
// first, otherwise it just hashes the pointer.
template <>
::std::size_t StringSpecific<const char *>::HashKey(const char *const &key) {
  return ::std::hash<::std::string>()(::std::string(key));
}

}  // namespace internal


template <class KeyType, class ValueType>
SharedHashmap<KeyType, ValueType>::SharedHashmap(int offset, int num_buckets)
    : pool_(Pool::GetPool()), num_buckets_(num_buckets) {
  // Check to see if the memory we want has already been allocated. If it has,
  // we assume that someone has already made a hashtable at this offset, and we
  // can just use it.
  if (!pool_->IsMemoryUsed(offset)) {
    // Allocate the underlying array in shared memory.
    data_ = pool_->AllocateForArrayAt<Bucket>(offset, num_buckets_);
    assert(data_ && "Failed to allocate shared hash table.");

    // Initialize the buckets.
    for (int i = 0; i < num_buckets_; ++i) {
      data_[i].next = nullptr;
      data_[i].occupied = false;
    }

    // Initialize the mutex.
    lock_ = pool_->AllocateForType<Mutex>();
    MutexInit(lock_);

  } else {
    // Just use the existing memory.
    data_ = pool_->AtOffset<Bucket>(offset);
  }
}

template <class KeyType, class ValueType>
void SharedHashmap<KeyType, ValueType>::Free() {
  // Free all the buckets.
  for (int i = 0; i < num_buckets_; ++i) {
    // We need to free everything in the linked list.
    Bucket *next = data_[i].next;
    while (next) {
      Bucket *to_free = next;
      next = next->next;
      pool_->FreeType<Bucket>(to_free);
    }
  }

  // Free the base array.
  pool_->FreeArray<Bucket>(data_, num_buckets_);

  // Free the mutex.
  pool_->FreeType<Mutex>(lock_);
}

template <class KeyType, class ValueType>
typename SharedHashmap<KeyType, ValueType>::Bucket *
SharedHashmap<KeyType, ValueType>::FindBucket(const KeyType &key) {
  // First, hash the key.
  ::std::size_t location = internal::StringSpecific<KeyType>::HashKey(key);
  // Bound it to our array size.
  location %= num_buckets_;

  Bucket *bucket = data_ + location;
  Bucket *end_bucket = nullptr;
  while (bucket) {
    if (bucket->occupied) {
      // Something's there already.
      if (internal::StringSpecific<KeyType>::CompareKeys(key, bucket->key)) {
        // This is the bucket we're looking for. We're done.
        return bucket;
      }

      // Otherwise, we have to keep looking down the linked list.
      end_bucket = bucket;
      bucket = bucket->next;

    } else {
      // If it's not occupied already, we can occupy it ourselves.
      return bucket;
    }
  }

  // We should always find some bucket assuming the map is working
  // correctly.
  assert(end_bucket != nullptr);
  // It's not at this position.
  return end_bucket;
}

template <class KeyType, class ValueType>
void SharedHashmap<KeyType, ValueType>::AddOrSet(const KeyType &key,
                                                 const ValueType &value) {
  MutexGrab(lock_);

  Bucket *bucket = FindBucket(key);

  if (bucket->occupied &&
      !internal::StringSpecific<KeyType>::CompareKeys(bucket->key, key)) {
    // It's not at this position. We have to add a new bucket to the linked
    // list.
    Bucket *new_bucket = pool_->AllocateForType<Bucket>();
    bucket->next = new_bucket;
    new_bucket->next = nullptr;
    bucket = new_bucket;
  }

  bucket->value = value;
  bucket->occupied = true;

  bucket->key = internal::StringSpecific<KeyType>::ConvertKey(key);

  MutexRelease(lock_);
}

template <class KeyType, class ValueType>
bool SharedHashmap<KeyType, ValueType>::Fetch(const KeyType &key,
                                              ValueType *value) {
  MutexGrab(lock_);

  Bucket *bucket = FindBucket(key);

  if (!internal::StringSpecific<KeyType>::CompareKeys(key, bucket->key)) {
    // It's not there.
    MutexRelease(lock_);
    return false;
  }

  MutexRelease(lock_);
  *value = bucket->value;

  return true;
}
