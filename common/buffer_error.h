#ifndef COMMON_BUFFER_ERROR_H
#define COMMON_BUFFER_ERROR_H

#include <exception>

#include "common_fwd.h"

namespace TOPNSPC::buffer {

struct end_of_buffer : std::exception {};

struct malformed_input : std::exception {
    explicit malformed_input(const char* msg) noexcept
        : msg_(msg) {}
    const char* what() const noexcept override {
        return msg_;
    }

private:
    const char* msg_;
};

}  // namespace TOPNSPC::buffer

#endif  // COMMON_BUFFER_ERROR_H
