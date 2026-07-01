function(add_blk_test name source)
    add_executable(${name} ${source})
    target_include_directories(${name} PRIVATE ${PROJECT_SOURCE_DIR})
    target_link_libraries(${name}
        PRIVATE
            blk
            clab_test_helpers
            GTest::gtest_main
    )
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_blk_test(test_aio test_aio.cc)
add_blk_test(test_io_context test_io_context.cc)
add_blk_test(test_kernel_device test_kernel_device.cc)

add_blk_test(test_avl_allocator test_avl_allocator.cc)
add_blk_test(test_bitmap_allocator test_bitmap_allocator.cc)
add_blk_test(test_hybrid_allocator test_hybrid_allocator.cc)
