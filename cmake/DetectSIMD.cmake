# cmake/DetectSIMD.cmake
# -------------------------------------------------------------------
# SIMD and Architecture Extension Auto-Detection Module
# -------------------------------------------------------------------

include(CheckCXXCompilerFlag)

# Initialize all feature flags as global cache variables (FALSE by default)
set(HAVE_ARM FALSE CACHE INTERNAL "")
set(HAVE_ARMV8_CRC FALSE CACHE INTERNAL "")
set(HAVE_ARMV8_CRC_CRYPTO_INTRINSICS FALSE CACHE INTERNAL "")
set(HAVE_ARMV8_CRYPTO FALSE CACHE INTERNAL "")
set(HAVE_ARMV8_SIMD FALSE CACHE INTERNAL "")
set(HAVE_ARM_NEON FALSE CACHE INTERNAL "")

set(HAVE_INTEL FALSE CACHE INTERNAL "")
set(HAVE_INTEL_SSE FALSE CACHE INTERNAL "")
set(HAVE_INTEL_SSE2 FALSE CACHE INTERNAL "")
set(HAVE_INTEL_SSE3 FALSE CACHE INTERNAL "")
set(HAVE_INTEL_SSSE3 FALSE CACHE INTERNAL "")
set(HAVE_INTEL_PCLMUL FALSE CACHE INTERNAL "")
set(HAVE_INTEL_SSE4_1 FALSE CACHE INTERNAL "")
set(HAVE_INTEL_SSE4_2 FALSE CACHE INTERNAL "")

set(HAVE_PPC64LE FALSE CACHE INTERNAL "")
set(HAVE_PPC64 FALSE CACHE INTERNAL "")
set(HAVE_PPC FALSE CACHE INTERNAL "")

# Global accumulator for target-specific SIMD compile flags
set(SIMD_COMPILE_FLAGS "" CACHE INTERNAL "")

# Normalize processor name to lowercase for robust string matching
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" TARGET_ARCH_LOWER)

# -------------------------------------------------------------------
# Branch A: ARM 64-bit (AArch64)
# -------------------------------------------------------------------
if(TARGET_ARCH_LOWER MATCHES "aarch64")
    set(HAVE_ARM TRUE CACHE INTERNAL "")

    check_cxx_compiler_flag("-march=armv8-a+crc+crypto" HAVE_ARMV8_CRC_CRYPTO_INTRINSICS)
    if(HAVE_ARMV8_CRC_CRYPTO_INTRINSICS)
        set(ARMV8_CRC_COMPILE_FLAGS "-march=armv8-a+crc+crypto" CACHE INTERNAL "")
        set(HAVE_ARMV8_CRC TRUE CACHE INTERNAL "")
        set(HAVE_ARMV8_CRYPTO TRUE CACHE INTERNAL "")
    else()
        check_cxx_compiler_flag("-march=armv8-a+crc" HAVE_ARMV8_CRC)
        check_cxx_compiler_flag("-march=armv8-a+crypto" HAVE_ARMV8_CRYPTO)
        if(HAVE_ARMV8_CRC)
            set(ARMV8_CRC_COMPILE_FLAGS "-march=armv8-a+crc" CACHE INTERNAL "")
        elseif(HAVE_ARMV8_CRYPTO)
            set(ARMV8_CRC_COMPILE_FLAGS "-march=armv8-a+crypto" CACHE INTERNAL "")
        endif()
    endif()

    check_cxx_compiler_flag("-march=armv8-a+simd" HAVE_ARMV8_SIMD)
    if(HAVE_ARMV8_SIMD)
        set(SIMD_COMPILE_FLAGS "${SIMD_COMPILE_FLAGS} -march=armv8-a+simd" CACHE INTERNAL "")
    endif()

# -------------------------------------------------------------------
# Branch B: ARM 32-bit
# -------------------------------------------------------------------
elseif(TARGET_ARCH_LOWER MATCHES "arm")
    set(HAVE_ARM TRUE CACHE INTERNAL "")
    check_cxx_compiler_flag("-mfpu=neon" HAVE_ARM_NEON)
    if(HAVE_ARM_NEON)
        set(SIMD_COMPILE_FLAGS "${SIMD_COMPILE_FLAGS} -mfpu=neon" CACHE INTERNAL "")
    endif()

# -------------------------------------------------------------------
# Branch C: Intel / AMD (X86_64 / i386)
# -------------------------------------------------------------------
elseif(TARGET_ARCH_LOWER MATCHES "x86_64" OR TARGET_ARCH_LOWER MATCHES "amd64" OR TARGET_ARCH_LOWER MATCHES "i386" OR TARGET_ARCH_LOWER MATCHES "i686")
    set(HAVE_INTEL TRUE CACHE INTERNAL "")

    macro(append_intel_flag flag_param cache_var)
        check_cxx_compiler_flag("${flag_param}" ${cache_var})
        if(${cache_var})
            set(SIMD_COMPILE_FLAGS "${SIMD_COMPILE_FLAGS} ${flag_param}" CACHE INTERNAL "")
        endif()
    endmacro()

    append_intel_flag("-msse"     HAVE_INTEL_SSE)
    append_intel_flag("-msse2"    HAVE_INTEL_SSE2)
    append_intel_flag("-msse3"    HAVE_INTEL_SSE3)
    append_intel_flag("-mssse3"   HAVE_INTEL_SSSE3)
    append_intel_flag("-mpclmul"  HAVE_INTEL_PCLMUL)
    append_intel_flag("-msse4.1"  HAVE_INTEL_SSE4_1)
    append_intel_flag("-msse4.2"  HAVE_INTEL_SSE4_2)

# -------------------------------------------------------------------
# Branch D: PowerPC (PPC64)
# -------------------------------------------------------------------
elseif(TARGET_ARCH_LOWER MATCHES "powerpc" OR TARGET_ARCH_LOWER MATCHES "ppc")
    if(TARGET_ARCH_LOWER MATCHES "ppc64le")
        set(HAVE_PPC64LE TRUE CACHE INTERNAL "")
    elseif(TARGET_ARCH_LOWER MATCHES "ppc64")
        set(HAVE_PPC64 TRUE CACHE INTERNAL "")
    else()
        set(HAVE_PPC TRUE CACHE INTERNAL "")
    endif()

    check_cxx_compiler_flag("-maltivec" HAS_ALTIVEC)
    if(HAS_ALTIVEC)
        add_compile_options(-maltivec)
    endif()

    check_cxx_compiler_flag("-mcpu=power8" HAVE_POWER8)
endif()

# -------------------------------------------------------------------
# Project flags to global preprocessor macros
# -------------------------------------------------------------------
macro(sync_macro_definition cmake_var macro_name)
    if(${cmake_var})
        add_compile_definitions(${macro_name}=1)
    endif()
endmacro()

sync_macro_definition(HAVE_ARMV8_CRC    HAVE_ARMV8_CRC)
sync_macro_definition(HAVE_ARMV8_CRYPTO HAVE_ARMV8_CRYPTO)
sync_macro_definition(HAVE_ARM_NEON     HAVE_ARM_NEON)
sync_macro_definition(HAVE_INTEL_SSE4_2 HAVE_INTEL_SSE4_2)
