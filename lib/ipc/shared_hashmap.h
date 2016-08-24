#ifndef GAIA_LIB_IPC_SHARED_HASHMAP_H_
#define GAIA_LIB_IPC_SHARED_HASHMAP_H_

#include <assert.h>
#include <string.h>

#include <functional>
#include <string>

#include "mutex.h"
#include "pool.h"

namespace gaia {
namespace ipc {

// A hashmap structure that is stored in shared memory.
//
// NOTE: Do not use as keys or values anything that is not trivially copyable.
// The only exception is C strings, which can be safely used as keys.
template <class KeyType, class ValueType>
class SharedHashmap {
 public:
  // Args:
  //  offset: The location in memory where the map will be created.
  //  num_buckets: The number of "buckets" for storing items the map will have.
  SharedHashmap(int offset, int num_buckets);

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
    KeyType key;
    // The actual value stored here.
    ValueType value;
    // A pointer to the next node in a linked list. This is used if we have
    // conflicts.
    Bucket *next;
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
  // The underlying array used to store data. This is located in SHM.
  Bucket *data_;
  // A mutex that we use to protect concurrent hashtable operations.
  Mutex *lock_;

  // The number of "buckets" for storing items the map will have.
  int num_buckets_;
};

#include "shared_hashmap_impl.h"

}  // namespace ipc
}  // namespace gaia

#endif // GAIA_LIB_INTERNAL_SHARED_HASHMAP_H_
