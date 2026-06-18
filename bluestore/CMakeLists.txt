add_library(bluestore STATIC
    allocator.cc
    avl_allocator.cc
    bitmap_allocator.cc
    bitmap_freelist_manager.cc
)
target_include_directories(bluestore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)
target_link_libraries(bluestore PUBLIC
    common
    kv
)
