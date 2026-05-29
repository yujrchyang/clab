#ifndef COMMON_BUFFER_FWD_H
#define COMMON_BUFFER_FWD_H

#include "common_fwd.h"

namespace TOPNSPC::buffer {
class ptr;
class list;
class hash;
}  // namespace TOPNSPC::buffer

namespace TOPNSPC {
using bufferptr = buffer::ptr;
using bufferlist = buffer::list;
using bufferhash = buffer::hash;
}  // namespace TOPNSPC

#endif  // COMMON_BUFFER_FWD_H
