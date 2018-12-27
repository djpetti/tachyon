#include <string.h>

#include "pool.h"
#include "string_specific.h"

namespace tachyon {
namespace shared_hashmap {

template <>
uintptr_t StringSpecific<const char *, uintptr_t>::ConvertKey(
    const char *const &key) {
  const int key_length = strlen(key) + 1;  // Include \0.

  Pool *pool = Pool::GetPool();
  char *shared_key = reinterpret_cast<char *>(pool->Allocate(key_length));
  assert(shared_key && "Allocating SHM failed unexpectedly.");
  memcpy(shared_key, key, key_length);

  // Get the offset in the pool.
  return pool->GetOffset(shared_key);
}

template <>
bool StringSpecific<const char *, uintptr_t>::CompareKeys(
    const uintptr_t &bucket_key, const char *const &user_key) {
  // Get the actual pointers.
  Pool *pool = Pool::GetPool();
  const char *bucket_key_ptr = pool->AtOffset<const char>(bucket_key);

  if (!bucket_key_ptr || !user_key) {
    // A special case is if one of them is null.
    return bucket_key_ptr == user_key;
  }

  return !strcmp(bucket_key_ptr, user_key);
}

template <>
::std::size_t StringSpecific<const char *, uintptr_t>::HashKey(
    const char *const &key) {
  return ::std::hash<::std::string>()(::std::string(key));
}


}  // namespace shared_hashmap
}  // namespace tachyon
