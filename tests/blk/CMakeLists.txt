add_executable(test_aio test_aio.cc)
target_include_directories(test_aio PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(test_aio
    PRIVATE
        blk
        GTest::gtest_main
)
add_test(NAME test_aio COMMAND test_aio)

add_executable(test_io_context test_io_context.cc)
target_include_directories(test_io_context PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(test_io_context
    PRIVATE
        blk
        GTest::gtest_main
)
add_test(NAME test_io_context COMMAND test_io_context)

add_executable(test_kernel_device test_kernel_device.cc)
target_include_directories(test_kernel_device PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(test_kernel_device
    PRIVATE
        blk
        GTest::gtest_main
)
add_test(NAME test_kernel_device COMMAND test_kernel_device)
