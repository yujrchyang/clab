find_package(fmt CONFIG REQUIRED)

add_executable(test_logger test_logger.cpp)
target_include_directories(test_logger PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(test_logger
    PRIVATE
        common
        fmt::fmt
        GTest::gtest_main
)
add_test(NAME test_logger COMMAND test_logger)
