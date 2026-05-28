add_executable(test_page test_page.cc)
target_include_directories(test_page PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(test_page
    PRIVATE
        GTest::gtest_main
)
add_test(NAME test_page COMMAND test_page)

add_executable(test_logger test_logger.cc)
target_include_directories(test_logger PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(test_logger
    PRIVATE
        common
        fmt::fmt
        GTest::gtest_main
)
add_test(NAME test_logger COMMAND test_logger)

add_executable(test_valgrind test_valgrind.cc)
target_include_directories(test_valgrind PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(test_valgrind
    PRIVATE
        GTest::gtest_main
)
add_test(NAME test_valgrind COMMAND test_valgrind)

add_executable(test_intarith test_intarith.cc)
target_include_directories(test_intarith PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(test_intarith
    PRIVATE
        GTest::gtest_main
)
add_test(NAME test_intarith COMMAND test_intarith)

add_executable(test_inline_memory test_inline_memory.cc)
target_include_directories(test_inline_memory PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(test_inline_memory
    PRIVATE
        GTest::gtest_main
)
add_test(NAME test_inline_memory COMMAND test_inline_memory)

add_executable(test_stack_string_stream test_stack_string_stream.cc)
target_include_directories(test_stack_string_stream PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(test_stack_string_stream
    PRIVATE
        GTest::gtest_main
)
add_test(NAME test_stack_string_stream COMMAND test_stack_string_stream)

add_executable(test_safe_io test_safe_io.cc)
target_include_directories(test_safe_io PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(test_safe_io
    PRIVATE
        common
        GTest::gtest_main
)
add_test(NAME test_safe_io COMMAND test_safe_io)

add_executable(test_error test_error.cc)
target_include_directories(test_error PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(test_error
    PRIVATE
        common
        GTest::gtest_main
)
add_test(NAME test_error COMMAND test_error)

add_executable(test_scope_guard test_scope_guard.cc)
target_include_directories(test_scope_guard PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(test_scope_guard
    PRIVATE
        GTest::gtest_main
)
add_test(NAME test_scope_guard COMMAND test_scope_guard)

add_executable(test_spinlock test_spinlock.cc)
target_include_directories(test_spinlock PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(test_spinlock
    PRIVATE
        common
        GTest::gtest_main
)
add_test(NAME test_spinlock COMMAND test_spinlock)
