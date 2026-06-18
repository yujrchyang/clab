add_executable(test_bitmap_freelist_manager test_bitmap_freelist_manager.cc)
target_include_directories(test_bitmap_freelist_manager PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(test_bitmap_freelist_manager
    PRIVATE
        bluestore
        kv
        clab_test_helpers
        GTest::gtest_main
)
add_test(NAME test_bitmap_freelist_manager COMMAND test_bitmap_freelist_manager)

add_executable(test_avl_allocator test_avl_allocator.cc)
target_include_directories(test_avl_allocator PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(test_avl_allocator
    PRIVATE
        bluestore
        clab_test_helpers
        GTest::gtest_main
)
add_test(NAME test_avl_allocator COMMAND test_avl_allocator)

add_executable(test_bitmap_allocator test_bitmap_allocator.cc)
target_include_directories(test_bitmap_allocator PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(test_bitmap_allocator
    PRIVATE
        bluestore
        clab_test_helpers
        GTest::gtest_main
)
add_test(NAME test_bitmap_allocator COMMAND test_bitmap_allocator)
