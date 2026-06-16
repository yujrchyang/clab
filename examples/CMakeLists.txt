add_executable(demo_kv_rocksdb demo_kv_rocksdb.cc)
target_include_directories(demo_kv_rocksdb PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(demo_kv_rocksdb
    PRIVATE
        kv
        common
)

add_executable(demo_blk_kernel demo_blk_kernel.cc)
target_include_directories(demo_blk_kernel PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(demo_blk_kernel
    PRIVATE
        blk
)


