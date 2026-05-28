# cmake/ConfigureChecks.cmake
# -------------------------------------------------------------------
# System API, Headers, and Storage Engine Feature Detection
# -------------------------------------------------------------------

include(CheckIncludeFileCXX)
include(CheckCXXSymbolExists)
include(CheckCXXSourceCompiles)
include(CheckFunctionExists)
include(CheckTypeSize)
include(TestBigEndian)

# ===================================================================
# 1. Helper Macro to Automatically Project Flags to C++ Compiler
# ===================================================================
macro(sync_to_compiler cmake_var macro_name)
    if(${cmake_var})
        add_compile_definitions(${macro_name}=1)
    endif()
endmacro()

# ===================================================================
# 2. High-Performance Storage System API Detection
# ===================================================================
check_cxx_symbol_exists(posix_fadvise   "fcntl.h"        HAS_POSIX_FADVISE)
check_cxx_symbol_exists(posix_fallocate "fcntl.h"        HAS_POSIX_FALLOCATE)
check_cxx_symbol_exists(sync_file_range "fcntl.h"        HAS_SYNC_FILE_RANGE)
check_cxx_symbol_exists(pipe2           "unistd.h"       HAS_PIPE2)
check_cxx_symbol_exists(fdatasync       "unistd.h"       HAS_FDATASYNC)
check_cxx_symbol_exists(eventfd         "sys/eventfd.h"  HAS_EVENTFD)
check_cxx_symbol_exists(syncfs          "unistd.h"       HAS_SYS_SYNCFS)

sync_to_compiler(HAS_POSIX_FADVISE   HAVE_POSIX_FADVISE)
sync_to_compiler(HAS_POSIX_FALLOCATE HAVE_POSIX_FALLOCATE)
sync_to_compiler(HAS_SYNC_FILE_RANGE HAVE_SYNC_FILE_RANGE)
sync_to_compiler(HAS_PIPE2           HAVE_PIPE2)
sync_to_compiler(HAS_FDATASYNC       HAVE_FDATASYNC)
sync_to_compiler(HAS_EVENTFD         HAVE_EVENTFD)
sync_to_compiler(HAS_SYS_SYNCFS      HAVE_SYS_SYNCFS)

# ===================================================================
# 3. Low-Level Linux System Headers Detection
# ===================================================================
check_include_file_cxx("sys/mount.h"     HAS_SYS_MOUNT_H)
check_include_file_cxx("sys/vfs.h"       HAS_SYS_VFS_H)
check_include_file_cxx("sys/param.h"     HAS_SYS_PARAM_H)
check_include_file_cxx("sys/types.h"     HAS_SYS_TYPES_H)
check_include_file_cxx("sys/prctl.h"     HAS_SYS_PRCTL_H)
check_include_file_cxx("linux/types.h"   HAS_LINUX_TYPES_H)
check_include_file_cxx("linux/version.h" HAS_LINUX_VERSION_H)
check_include_file_cxx("execinfo.h"      HAS_EXECINFO_H)

sync_to_compiler(HAS_SYS_MOUNT_H     HAVE_SYS_MOUNT_H)
sync_to_compiler(HAS_SYS_VFS_H       HAVE_SYS_VFS_H)
sync_to_compiler(HAS_SYS_PARAM_H     HAVE_SYS_PARAM_H)
sync_to_compiler(HAS_SYS_TYPES_H     HAVE_SYS_TYPES_H)
sync_to_compiler(HAS_SYS_PRCTL_H     HAVE_SYS_PRCTL_H)
sync_to_compiler(HAS_LINUX_TYPES_H   HAVE_LINUX_TYPES_H)
sync_to_compiler(HAS_LINUX_VERSION_H HAVE_LINUX_VERSION_H)
sync_to_compiler(HAS_EXECINFO_H      HAVE_EXECINFO_H)

# ===================================================================
# 4. Target Architecture Endianness Detection
# ===================================================================
test_big_endian(BIG_ENDIAN)
if(NOT BIG_ENDIAN)
    add_compile_definitions(LITTLE_ENDIAN=1)
else()
    add_compile_definitions(BIG_ENDIAN=1)
endif()

# ===================================================================
# 5. POSIX Threading Runtime Feature Detection
# ===================================================================
set(CMAKE_REQUIRED_LIBRARIES pthread)

check_function_exists(pthread_spin_init           HAVE_PTHREAD_SPINLOCK)
check_function_exists(pthread_set_name_np         HAVE_PTHREAD_SET_NAME_NP)
check_function_exists(pthread_get_name_np         HAVE_PTHREAD_GET_NAME_NP)
check_function_exists(pthread_setname_np          HAVE_PTHREAD_SETNAME_NP)
check_function_exists(pthread_getname_np          HAVE_PTHREAD_GETNAME_NP)
check_function_exists(pthread_rwlockattr_setkind_np HAVE_PTHREAD_RWLOCKATTR_SETKIND_NP)

unset(CMAKE_REQUIRED_LIBRARIES)

sync_to_compiler(HAVE_PTHREAD_SPINLOCK               HAVE_PTHREAD_SPINLOCK)
sync_to_compiler(HAVE_PTHREAD_SET_NAME_NP            HAVE_PTHREAD_SET_NAME_NP)
sync_to_compiler(HAVE_PTHREAD_GET_NAME_NP            HAVE_PTHREAD_GET_NAME_NP)
sync_to_compiler(HAVE_PTHREAD_SETNAME_NP             HAVE_PTHREAD_SETNAME_NP)
sync_to_compiler(HAVE_PTHREAD_GETNAME_NP             HAVE_PTHREAD_GETNAME_NP)
sync_to_compiler(HAVE_PTHREAD_RWLOCKATTR_SETKIND_NP  HAVE_PTHREAD_RWLOCKATTR_SETKIND_NP)

# ===================================================================
# 6. Compiler Built-in / Language Feature Detection
# ===================================================================
macro(check_compiler_feature nice_name source_expr)
    check_cxx_source_compiles("
        int main() {
            const auto *p = ${source_expr};
            (void)p;
            return 0;
        }
    " HAS_${nice_name})
    sync_to_compiler(HAS_${nice_name} HAVE_${nice_name})
endmacro()

check_compiler_feature(PRETTY_FUNC "__PRETTY_FUNCTION__")
check_compiler_feature(FUNC "__func__")

# ===================================================================
# 8. Third-Party Storage Middleware and I/O Engines Detection
# ===================================================================
find_package(PkgConfig REQUIRED)

pkg_check_modules(liburing QUIET liburing)
if(liburing_FOUND)
    add_compile_definitions(HAVE_LIBURING=1)
endif()

pkg_check_modules(libaio QUIET libaio)
if(libaio_FOUND)
    add_compile_definitions(HAVE_LIBAIO=1)
endif()
