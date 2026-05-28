#ifndef COMMON_BUFFER_ERROR_H
#define COMMON_BUFFER_ERROR_H

#include <stdexcept>

#include "common_fwd.h"

namespace TOPNSPC::buffer {

struct error : std::runtime_error {
    using runtime_error::runtime_error;
    error() : runtime_error("buffer::error") {}
};

struct end_of_buffer : error {};

struct malformed_input : error {
    using error::error;
};

}  // namespace TOPNSPC::buffer

#endif  // COMMON_BUFFER_ERROR_H
