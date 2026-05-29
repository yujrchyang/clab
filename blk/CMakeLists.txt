add_library(blk SHARED
    aio.cc
)
target_include_directories(blk PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)
target_link_libraries(blk PUBLIC
    common
    aio
)
