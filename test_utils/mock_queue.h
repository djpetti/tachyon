#ifndef TACHYON_TEST_UTILS_MOCK_QUEUE_H_
#define TACHYON_TEST_UTILS_MOCK_QUEUE_H_

#include <stdint.h>

#include "gmock/gmock.h"

#include "lib/queue_interface.h"

namespace tachyon {
namespace testing {

// Mock class for queues.
template <class T>
class MockQueue : public QueueInterface<T> {
 public:
  MOCK_METHOD1_T(Enqueue, bool(const T &item));
  MOCK_METHOD1_T(EnqueueBlocking, bool(const T &item));

  MOCK_METHOD1_T(DequeueNext, bool(T *item));
  MOCK_METHOD1_T(DequeueNextBlocking, void(T *item));

  MOCK_CONST_METHOD0_T(GetOffset, int());
  MOCK_METHOD0_T(FreeQueue, void());

  MOCK_CONST_METHOD0_T(GetNumConsumers, uint32_t());
};

}  // namespace testing
}  // namespace tachyon

#endif  // TACHYON_TEST_UTILS_MOCK_QUEUE_H_
