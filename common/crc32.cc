#include "crc32.h"

#include <array>

#ifdef HAVE_ISA_L
extern "C" {
#include <isa-l.h>
}
#endif

namespace TOPNSPC {
namespace {

constexpr uint32_t POLY_CASTAGNOLI = 0x82F63B78;

class CRC32Table {
public:
    std::array<uint32_t, 256> table;

    constexpr CRC32Table() : table() {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t crc = i;
            for (uint32_t j = 0; j < 8; ++j) {
                if (crc & 1) {
                    crc = (crc >> 1) ^ POLY_CASTAGNOLI;
                } else {
                    crc >>= 1;
                }
            }
            table[i] = crc;
        }
    }
};

constexpr CRC32Table g_crc32c_table;

}  // anonymous namespace

uint32_t crc32c_software_fallback(const uint8_t *data, size_t length, uint32_t previous_crc) {
    uint32_t crc = ~previous_crc;
    for (size_t i = 0; i < length; ++i) {
        uint8_t index = static_cast<uint8_t>((crc ^ data[i]) & 0xFF);
        crc = (crc >> 8) ^ g_crc32c_table.table[index];
    }
    return ~crc;
}

uint32_t calc_crc32(const uint8_t *data, size_t length, uint32_t previous_crc) {
    if (length == 0) {
        return previous_crc;
    }
    if (data == nullptr) {
        // CRC of a zero-filled buffer of length bytes
        uint32_t crc = ~previous_crc;
        for (size_t i = 0; i < length; ++i) {
            uint8_t index = static_cast<uint8_t>(crc & 0xFF);
            crc = (crc >> 8) ^ g_crc32c_table.table[index];
        }
        return ~crc;
    }

#ifdef HAVE_ISA_L
    return ~crc32_iscsi(const_cast<uint8_t *>(data), static_cast<int>(length), ~previous_crc);
#else
    return crc32c_software_fallback(data, length, previous_crc);
#endif
}

}  // namespace TOPNSPC
