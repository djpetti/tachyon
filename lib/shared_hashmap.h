#ifndef TACHYON_LIB_IPC_SHARED_HASHMAP_H_
#define TACHYON_LIB_IPC_SHARED_HASHMAP_H_

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <functional>
#include <string>

#include "mutex.h"
#include "pool.h"

namespace tachyon {

// A hashmap structure that is stored in shared memory. Normally, the
// implementation with two template parameters should be used. This one is only
// for the case when the actual value stored in shared memory for the key is
// different from the keys that are passed in.
//
// NOTE: Do not use as keys or values anything that is not trivially copyable.
// The only exception is C strings, which can be safely used as keys.
template <class KeyType, class ConvKeyType, class ValueType>
class SharedHashmapInt {
 public:
  // Args:
  //  offset: The location in memory where the map will be created.
  //  num_buckets: The number of "buckets" for storing items the map will have.
  SharedHashmapInt(int offset, int num_buckets);

  // Add a new item to the map, or modify an existing item.
  // Args:
  //  key: The key of the item to add.
  //  value: The value of the item to add.
  void AddOrSet(const KeyType &key, const ValueType &value);

  // Gets the current value of an item in the map.
  // Args:
  //  key: The key of the item to fetch.
  //  value: Will be set to the fetched value.
  // Returns:
  //  True if the item exists, false otherwise.
  bool Fetch(const KeyType &key, ValueType *value);

  // Frees the underlying shared memory associated with this map.
  // IMPORTANT: Only call this method when you are sure that you and
  // everyone else in every other process are completely done with this map.
  void Free();

 private:
  // A particular location where items can be stored in the hashmap.
  struct Bucket {
    // Whether this bucket is occupied.
    bool occupied;
    // The key stored here.
    ConvKeyType key;
    // The actual value stored here.
    ValueType value;
    // A pointer to the next node in a linked list. This is used if we have
    // conflicts.
    Bucket *next;
  };

  // Structure that we use internally to organize all our state that goes in
  // SHM.
  struct ShmData {
    // The SHM offset of the underlying array that we use to store data.
    uintptr_t data_offset;
    // The SHM offset of the mutex that we use to protect concurrent hashtable
    // operations.
    uintptr_t lock_offset;
  };

  // Gets nearest bucket to where an element should go.
  // Args:
  //  key: The key of an element we're looking for.
  // Returns:
  //  A pointer to the bucket where the element that we want is, or, if that
  //  bucket does not exist, a pointer to the bucket coming before it in a
  //  linked list.
  Bucket *FindBucket(const KeyType &key);

  // The pool we use to store data.
  Pool *pool_;

  // Data located in SHM.
  ShmData *shm_;
  // Aliases to the contents of SHM.
  Bucket *data_;
  Mutex *lock_;

  // The number of "buckets" for storing items the map will have.
  int num_buckets_;
};

template <class KeyType, class ValueType>
class SharedHashmap {
 public:
  SharedHashmap(int offset, int num_buckets);

  void AddOrSet(const KeyType &key, const ValueType &value);
  bool Fetch(const KeyType &key, ValueType *value);
  void Free();

 private:
  // Internal fully-specialized SharedHashmap.
  SharedHashmapInt<KeyType, KeyType, ValueType> map_;
};

// Specialization for string keys.
template <class ValueType>
class SharedHashmap<const char *, ValueType> {
 public:
  SharedHashmap(int offset, int num_buckets);

  void AddOrSet(const char *key, const ValueType &value);
  bool Fetch(const char *key, ValueType *value);
  void Free();

 private:
  // Internal fully-specialized SharedHashmap.
  SharedHashmapInt<const char *, uintptr_t, ValueType> map_;
};

#include "shared_hashmap_impl.h"

}  // namespace tachyon

#endif  // TACHYON_LIB_INTERNAL_SHARED_HASHMAP_H_
