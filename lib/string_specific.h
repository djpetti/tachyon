#ifndef TACHYON_LIB_STRING_SPECIFIC_H_
#define TACHYON_LIB_STRING_SPECIFIC_H_

#include <string>

namespace tachyon {
namespace shared_hashmap {

// Here's a class whose sole purpose is to implement string-specific
// functionality for SharedHashmap.
template <class KeyType, class ConvKeyType>
class StringSpecific {
 public:
  // Converts a key, and returns what to set the bucket's Key value as.
  // Args:
  //  key: The key we want to set.
  static ConvKeyType ConvertKey(const KeyType &key) {
    // If it's trivially copyable, just use our normal key.
    return key;
  }

  // Compares two keys.
  // Args:
  //  bucket_key: The first key to compare, from the bucket.
  //  user_key: The second key to compare.
  // Returns:
  //  True if the keys are the same, false if they aren't.
  static bool CompareKeys(const ConvKeyType &bucket_key,
                          const KeyType &user_key) {
    return bucket_key == user_key;
  }

  // Hashes a key.
  // Args:
  //  The key to hash.
  // Returns:
  //  The hashed key.
  static ::std::size_t HashKey(const KeyType &key) {
    return ::std::hash<ConvKeyType>()(key);
  }
};

// Explicit specialization of ConvertKey for strings.
// This works the same way as the normal version, except it properly copies the
// string into shared memory.
template <>
uintptr_t StringSpecific<const char *, uintptr_t>::ConvertKey(
    const char *const &key);
// Explicit specialization of CompareKeys for strings.
// This works the same way as the normal version, except that it compares the
// strings character-by-character instead of merely comparing pointers.
template <>
bool StringSpecific<const char *, uintptr_t>::CompareKeys(
    const uintptr_t &bucket_key, const char *const &user_key);
// Explicit specialization of HashKey for strings.
// std::hash can only work with std::strings, so we need to convert it to one
// first, otherwise it just hashes the pointer.
template <>
::std::size_t StringSpecific<const char *, uintptr_t>::HashKey(
    const char *const &key);

}  // namespace shared_hashmap
}  // namespace tachyon

#endif  // TACHYON_LIB_STRING_SPECIFIC_H_
