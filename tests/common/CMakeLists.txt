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

add_executable(test_valgrind test_valgrind.cpp)
target_include_directories(test_valgrind PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(test_valgrind
    PRIVATE
        GTest::gtest_main
)
add_test(NAME test_valgrind COMMAND test_valgrind)

add_executable(test_inline_memory test_inline_memory.cpp)
target_include_directories(test_inline_memory PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(test_inline_memory
    PRIVATE
        GTest::gtest_main
)
add_test(NAME test_inline_memory COMMAND test_inline_memory)
