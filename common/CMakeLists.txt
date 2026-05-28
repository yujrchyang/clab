add_library(common SHARED
    logger.cc
    spinlock.cc
)
target_include_directories(common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    $<TARGET_PROPERTY:spdlog::spdlog,INTERFACE_INCLUDE_DIRECTORIES>
)
target_link_libraries(common PUBLIC
    Boost::boost
)
target_link_libraries(common PRIVATE
    -Wl,--whole-archive
    spdlog::spdlog
    -Wl,--no-whole-archive
    fmt::fmt
    pthread
)
