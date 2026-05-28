#ifndef COMMON_CRC32_H
#define COMMON_CRC32_H

#include <cstddef>
#include <cstdint>

#include "common_fwd.h"

namespace TOPNSPC {

uint32_t calc_crc32(const uint8_t *data, size_t length, uint32_t previous_crc = 0);

}  // namespace TOPNSPC

#endif  // COMMON_CRC32_H
