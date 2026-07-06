#ifndef COMMON_BYTEORDER_H
#define COMMON_BYTEORDER_H

#include <linux/types.h>

#include <boost/endian/conversion.hpp>

template <typename T>
inline T swab(T val) {
    return boost::endian::endian_reverse(val);
}

template <typename T>
struct cxxlab_le {
private:
    T v;

public:
    cxxlab_le() = default;
    explicit cxxlab_le(T nv)
        : v{boost::endian::native_to_little(nv)} {}
    cxxlab_le<T> &operator=(T nv) {
        v = boost::endian::native_to_little(nv);
        return *this;
    }
    operator T() const { return boost::endian::little_to_native(v); }
    // compares little-endian byte storage directly (equivalent to decoded comparison)
    friend inline bool operator==(cxxlab_le a, cxxlab_le b) {
        return a.v == b.v;
    }
} __attribute__((packed));

using cxxlab_le64 = cxxlab_le<__u64>;
using cxxlab_le32 = cxxlab_le<__u32>;
using cxxlab_le16 = cxxlab_le<__u16>;

using cxxlab_les64 = cxxlab_le<__s64>;
using cxxlab_les32 = cxxlab_le<__s32>;
using cxxlab_les16 = cxxlab_le<__s16>;

inline cxxlab_les64 init_les64(__s64 x) {
    cxxlab_les64 v;
    v = x;
    return v;
}
inline cxxlab_les32 init_les32(__s32 x) {
    cxxlab_les32 v;
    v = x;
    return v;
}
inline cxxlab_les16 init_les16(__s16 x) {
    cxxlab_les16 v;
    v = x;
    return v;
}

#endif  // COMMON_BYTEORDER_H
