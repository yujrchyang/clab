add_library(kv SHARED
    key_value_db.cc
    mem/mem_db.cc
    rocksdb/rocksdb_store.cc
)
target_include_directories(kv PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)
target_link_libraries(kv PUBLIC
    common
)
target_link_libraries(kv PRIVATE
    RocksDB::RocksDB
)
