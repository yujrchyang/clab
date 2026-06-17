#pragma once

#include <string>

#include "common/common_fwd.h"

namespace TOPNSPC {

class MergeOperator {
public:
    virtual ~MergeOperator() = default;

    virtual void merge_nonexistent(const char *rdata, size_t rlen,
                                   std::string *new_value) = 0;

    virtual void merge(const char *ldata, size_t llen,
                       const char *rdata, size_t rlen,
                       std::string *new_value) = 0;

    virtual const char *name() const = 0;
};

}  // namespace TOPNSPC
