add_library(blk SHARED
    aio.cc
    allocator.cc
    avl_allocator.cc
    bitmap_allocator.cc
    block_device.cc
    hybrid_allocator.cc
    io_context.cc
    kernel_device.cc
)
target_include_directories(blk PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)
target_link_libraries(blk PUBLIC
    common
    aio
)
