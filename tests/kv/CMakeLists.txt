add_executable(test_librocksdb test_librocksdb.cc)
target_include_directories(test_librocksdb PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(test_librocksdb
    PRIVATE
        RocksDB::RocksDB
        clab_test_helpers
        GTest::gtest_main
        pthread
        dl
        rt
)
add_test(NAME test_librocksdb COMMAND test_librocksdb)

add_executable(test_rocksdb test_rocksdb.cc)
target_include_directories(test_rocksdb PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(test_rocksdb
    PRIVATE
        kv
        RocksDB::RocksDB
        clab_test_helpers
        GTest::gtest_main
        pthread
        dl
        rt
)
add_test(NAME test_rocksdb COMMAND test_rocksdb)

add_executable(test_memdb test_memdb.cc)
target_include_directories(test_memdb PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(test_memdb
    PRIVATE
        kv
        clab_test_helpers
        GTest::gtest_main
)
add_test(NAME test_memdb COMMAND test_memdb)
