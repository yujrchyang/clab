add_library(common SHARED
    crc32.cc
    error.cc
    logger.cc
    safe_io.cc
    spinlock.cc
)
target_include_directories(common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    $<TARGET_PROPERTY:spdlog::spdlog,INTERFACE_INCLUDE_DIRECTORIES>
)
target_link_libraries(common PUBLIC
    Boost::boost
    isal
)
target_link_libraries(common PRIVATE
    -Wl,--whole-archive
    spdlog::spdlog
    -Wl,--no-whole-archive
    fmt::fmt
    pthread
)
target_compile_definitions(common PUBLIC HAVE_ISA_L=1)
