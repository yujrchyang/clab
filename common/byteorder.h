#ifndef COMMON_BYTEORDER_H
#define COMMON_BYTEORDER_H

#include <boost/endian/conversion.hpp>
#include <linux/types.h>

template <typename T>
inline T swab(T val) {
    return boost::endian::endian_reverse(val);
}

template <typename T>
struct clab_le {
private:
    T v;

public:
    clab_le() = default;
    explicit clab_le(T nv)
        : v{boost::endian::native_to_little(nv)} {}
    clab_le<T> &operator=(T nv) {
        v = boost::endian::native_to_little(nv);
        return *this;
    }
    operator T() const { return boost::endian::little_to_native(v); }
    // compares little-endian byte storage directly (equivalent to decoded comparison)
    friend inline bool operator==(clab_le a, clab_le b) {
        return a.v == b.v;
    }
} __attribute__((packed));

using clab_le64 = clab_le<__u64>;
using clab_le32 = clab_le<__u32>;
using clab_le16 = clab_le<__u16>;

using clab_les64 = clab_le<__s64>;
using clab_les32 = clab_le<__s32>;
using clab_les16 = clab_le<__s16>;

inline clab_les64 init_les64(__s64 x) {
    clab_les64 v;
    v = x;
    return v;
}
inline clab_les32 init_les32(__s32 x) {
    clab_les32 v;
    v = x;
    return v;
}
inline clab_les16 init_les16(__s16 x) {
    clab_les16 v;
    v = x;
    return v;
}

#endif  // COMMON_BYTEORDER_H
