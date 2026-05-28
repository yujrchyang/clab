#ifndef COMMON_ARMOR_H
#define COMMON_ARMOR_H

#include <cstddef>

#include "common_fwd.h"

namespace TOPNSPC {

int armor(char *dst, char *const dst_end,
          const char *src, const char *end);

int armor_linebreak(char *dst, char *const dst_end,
                    const char *src, const char *end,
                    int line_width);

int unarmor(char *dst, char *const dst_end,
            const char *src, const char *const end);

}  // namespace TOPNSPC

#endif  // COMMON_ARMOR_H
