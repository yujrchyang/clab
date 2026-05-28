#ifndef COMMON_CPU_H
#define COMMON_CPU_H

#include "common_fwd.h"

namespace TOPNSPC {
namespace cpu {

void probe();

namespace features {
// x86
inline bool intel_pclmul = false;
inline bool intel_sse42 = false;
inline bool intel_sse41 = false;
inline bool intel_ssse3 = false;
inline bool intel_sse3 = false;
inline bool intel_sse2 = false;
inline bool intel_aesni = false;

// ARM / AArch64
inline bool neon = false;
inline bool aarch64_crc32 = false;
inline bool aarch64_pmull = false;
}  // namespace features

}  // namespace cpu
}  // namespace TOPNSPC

#endif  // COMMON_CPU_H
