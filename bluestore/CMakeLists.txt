add_library(bluestore STATIC
    allocator.cc
    avl_allocator.cc
    bitmap_allocator.cc
    hybrid_allocator.cc
    freelist_manager.cc
    bitmap_freelist_manager.cc
    bluefs.cc
    bluefs_config.cc
    bluefs_types.cc
    bluefs_volume_selector.cc
    blue_rocks_env.cc
)
target_include_directories(bluestore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)
target_link_libraries(bluestore PUBLIC
    common
    kv
)
target_link_libraries(bluestore PUBLIC
    RocksDB::RocksDB
)
