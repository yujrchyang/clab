#if defined(__x86_64__)
#include <cpuid.h>
#elif defined(__linux__) && (defined(__arm__) || defined(__aarch64__))
#include <asm/hwcap.h>
#include <sys/auxv.h>
#endif

#include "cpu.h"

namespace TOPNSPC {
namespace cpu {

void probe() {
#if defined(__x86_64__)
    unsigned int eax, ebx, ecx = 0, edx = 0;
    if (__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
        features::intel_pclmul = (ecx & (1 << 1)) != 0;
        features::intel_sse42 = (ecx & (1 << 20)) != 0;
        features::intel_sse41 = (ecx & (1 << 19)) != 0;
        features::intel_ssse3 = (ecx & (1 << 9)) != 0;
        features::intel_sse3 = (ecx & (1 << 0)) != 0;
        features::intel_sse2 = (edx & (1 << 26)) != 0;
        features::intel_aesni = (ecx & (1 << 25)) != 0;
    }

#elif defined(__aarch64__)
    unsigned long hwcap = getauxval(AT_HWCAP);
    features::neon = (hwcap & HWCAP_ASIMD) == HWCAP_ASIMD;
    features::aarch64_crc32 = (hwcap & HWCAP_CRC32) == HWCAP_CRC32;
    features::aarch64_pmull = (hwcap & HWCAP_PMULL) == HWCAP_PMULL;
#endif
}

namespace {
const bool _auto = (cpu::probe(), true);
}

}  // namespace cpu
}  // namespace TOPNSPC
