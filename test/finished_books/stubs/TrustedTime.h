#pragma once
#include <cstdint>

namespace trustedtime {
// Only finishedbooks::recordFinished() reads this, and these tests call
// markRead() directly with an explicit epoch instead.
inline int64_t fakeNow = 0;
inline int64_t trustedNow() { return fakeNow; }
}  // namespace trustedtime
