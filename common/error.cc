#include "error.h"

#include <system_error>

namespace TOPNSPC {

std::string cpp_strerror(int err) {
    if (err < 0)
        err = -err;
    return std::string("(") + std::to_string(err) + ") "
         + std::generic_category().message(err);
}

}  // namespace TOPNSPC
