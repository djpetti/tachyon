#ifndef GAIA_LIB_INTERNAL_CONSTANTS_H_
#define GAIA_LIB_INTERNAL_CONSTANTS_H_

// Contains constants that are used by various files in one place for easy
// modification.

namespace gaia {
namespace internal {

// How many items we want our queues to be able to hold. This constant
// designates how many times to << 1 in order to get that number.
static constexpr int kQueueCapacityShifts = 6; // 64
static constexpr int kQueueCapacity = 1 << kQueueCapacityShifts;
// Size to use when initializing the underlying pool.
// TODO (danielp): This will have to be increased eventually for actual Gaia
// stuff.
static constexpr int kPoolSize = 64000;
// The maximum number of consumers a queue can have.
static constexpr int kMaxConsumers = 64;

}
}

#endif // GAIA_LIB_INTERNAL_CONSTANTS_H_