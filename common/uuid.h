#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <string>

#include <fcntl.h>
#include <unistd.h>

#include "common/cassert.h"
#include "common/denc.h"

namespace TOPNSPC {

struct uuid_d {
    std::array<uint8_t, 16> uuid{};

    uuid_d() = default;

    bool is_zero() const {
        for (auto b : uuid)
            if (b != 0) return false;
        return true;
    }

    void generate() {
        int fd = open("/dev/urandom", O_RDONLY);
        clab_assert(fd >= 0);
        auto r = read(fd, uuid.data(), 16);
        clab_assert(r == 16);
        close(fd);
        // RFC 4122 version 4: set bits 12-15 of byte 7 to 0100
        uuid[6] = (uuid[6] & 0x0f) | 0x40;
        // RFC 4122 variant: set bits 6-7 of byte 9 to 10
        uuid[8] = (uuid[8] & 0x3f) | 0x80;
    }

    bool operator==(const uuid_d &o) const { return uuid == o.uuid; }
    bool operator!=(const uuid_d &o) const { return uuid != o.uuid; }
    bool operator<(const uuid_d &o) const { return uuid < o.uuid; }

    DENC(uuid_d, v, p) {
        DENC_START(1, 1, p);
        for (auto &b : v.uuid) denc(b, p);
        DENC_FINISH(p);
    }
};
WRITE_CLASS_DENC(uuid_d);

}  // namespace TOPNSPC
