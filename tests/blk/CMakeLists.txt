add_executable(test_aio test_aio.cc)
target_include_directories(test_aio PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(test_aio
    PRIVATE
        blk
        GTest::gtest_main
)
add_test(NAME test_aio COMMAND test_aio)
