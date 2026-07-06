add_executable(test_btier_types test_btier_types.cc)
target_include_directories(test_btier_types PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(test_btier_types
    PRIVATE
        btier
        cxxlab_test_helpers
        GTest::gtest_main
)
add_test(NAME test_btier_types COMMAND test_btier_types)

add_executable(test_config test_config.cc)
target_include_directories(test_config PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(test_config
    PRIVATE
        btier
        cxxlab_test_helpers
        GTest::gtest_main
)
add_test(NAME test_config COMMAND test_config)

add_executable(test_extent_map test_extent_map.cc)
target_include_directories(test_extent_map PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(test_extent_map
    PRIVATE
        btier
        cxxlab_test_helpers
        GTest::gtest_main
)
add_test(NAME test_extent_map COMMAND test_extent_map)

add_executable(test_key_map test_key_map.cc)
target_include_directories(test_key_map PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(test_key_map
    PRIVATE
        btier
        cxxlab_test_helpers
        GTest::gtest_main
)
add_test(NAME test_key_map COMMAND test_key_map)

add_executable(test_journal test_journal.cc)
target_include_directories(test_journal PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(test_journal
    PRIVATE
        btier
        blk
        cxxlab_test_helpers
        GTest::gtest_main
)
add_test(NAME test_journal COMMAND test_journal)

add_executable(test_btier test_btier.cc)
target_include_directories(test_btier PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(test_btier
    PRIVATE
        btier
        blk
        cxxlab_test_helpers
        GTest::gtest_main
)
add_test(NAME test_btier COMMAND test_btier)

add_executable(test_scoring test_scoring.cc)
target_include_directories(test_scoring PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(test_scoring
    PRIVATE
        btier
        blk
        cxxlab_test_helpers
        GTest::gtest_main
)
add_test(NAME test_scoring COMMAND test_scoring)

add_executable(test_migration test_migration.cc)
target_include_directories(test_migration PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(test_migration
    PRIVATE
        btier
        blk
        cxxlab_test_helpers
        GTest::gtest_main
)
add_test(NAME test_migration COMMAND test_migration)

add_executable(test_e2e test_e2e.cc)
target_include_directories(test_e2e PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(test_e2e
    PRIVATE
        btier
        blk
        cxxlab_test_helpers
        GTest::gtest_main
)
add_test(NAME test_e2e COMMAND test_e2e)

# ── TSAN support ──
option(BTIER_ENABLE_TSAN "Enable ThreadSanitizer for btier tests" OFF)
if(BTIER_ENABLE_TSAN)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        foreach(test_target
            test_btier test_scoring test_migration test_e2e
            test_extent_map test_key_map)
            if(TARGET ${test_target})
                target_compile_options(${test_target} PRIVATE -fsanitize=thread -g)
                target_link_options(${test_target} PRIVATE -fsanitize=thread)
            endif()
        endforeach()
    endif()
endif()
