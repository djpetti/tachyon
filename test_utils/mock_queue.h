#ifndef TACHYON_TEST_UTILS_MOCK_QUEUE_H_
#define TACHYON_TEST_UTILS_MOCK_QUEUE_H_

#include "gmock/gmock.h"

#include "lib/queue_interface.h"

namespace tachyon {
namespace testing {

// Mock class for queues.
template <class T>
class MockQueue : public QueueInterface<T> {
 public:
  MOCK_METHOD1(Enqueue, bool(const T &item));
  MOCK_METHOD1(EnqueueBlocking, bool(const T &item));

  MOCK_METHOD1(DequeueNext, bool(T *item));
  MOCK_METHOD1(DequeueNextBlocking, void(T *item));

  MOCK_METHOD0(GetOffset, int());
  MOCK_METHOD0(FreeQueue, void());
};

}  // namespace testing
}  // namespace tachyon

#endif  // TACHYON_TEST_UTILS_MOCK_QUEUE_H_
