add_executable(test_rocksdb test_rocksdb.cc)
target_include_directories(test_rocksdb PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(test_rocksdb
    PRIVATE
        RocksDB::RocksDB
        GTest::gtest_main
        pthread
        dl
        rt
)
add_test(NAME test_rocksdb COMMAND test_rocksdb)
