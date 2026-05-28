#ifndef COMMON_PAGE_H
#define COMMON_PAGE_H

#include <unistd.h>

#include "common_fwd.h"

namespace TOPNSPC {

struct page_info {
    unsigned size;
    unsigned long mask;
    unsigned shift;
};

inline const page_info &page() noexcept {
    static const page_info p = [] {
        unsigned sz = sysconf(_SC_PAGESIZE);
        return page_info{
            sz,
            ~(static_cast<unsigned long>(sz) - 1),
            static_cast<unsigned>(__builtin_ctz(sz))};
    }();
    return p;
}

}  // namespace TOPNSPC

#endif  // COMMON_PAGE_H
