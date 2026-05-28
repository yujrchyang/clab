#ifndef COMMON_SPINLOCK_HPP
#define COMMON_SPINLOCK_HPP

#include <atomic>

#include "common_fwd.h"

namespace TOPNSPC {

class spinlock {
public:
    void lock();
    void unlock();

private:
    std::atomic_flag lock_;
};

}  // namespace TOPNSPC

#endif  // COMMON_SPINLOCK_HPP
