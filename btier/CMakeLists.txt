add_library(btier SHARED
    btier.cc
    btier_observer.cc
    config.cc
    extent_map.cc
    journal.cc
    key_map.cc
    migration_engine.cc
    scoring_engine.cc
)

target_include_directories(btier PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(btier PUBLIC
    common
    blk
)

# ── TSAN support for concurrent testing ──
option(BTIER_ENABLE_TSAN "Enable ThreadSanitizer for btier tests" OFF)
if(BTIER_ENABLE_TSAN)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        target_compile_options(btier PRIVATE -fsanitize=thread -g)
        target_link_options(btier PRIVATE -fsanitize=thread)
    endif()
endif()
