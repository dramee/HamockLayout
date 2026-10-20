cmake_minimum_required(VERSION 3.16)
project(HammockLayout LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /utf-8 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_library(hammock
    src/graph.cpp
    src/xml.cpp
    src/graphml.cpp
    src/hammocks.cpp
)
target_include_directories(hammock PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(hammocks src/main.cpp)
target_link_libraries(hammocks PRIVATE hammock)

option(HAMMOCK_BUILD_TESTS "Собирать тесты" ON)
if(HAMMOCK_BUILD_TESTS)
    enable_testing()
    add_executable(hammock_tests
        tests/test_main.cpp
        tests/test_xml.cpp
        tests/test_graphml.cpp
        tests/test_numbering.cpp
        tests/test_hammocks.cpp
        tests/test_random.cpp
    )
    target_link_libraries(hammock_tests PRIVATE hammock)
    target_compile_definitions(hammock_tests PRIVATE
        HAMMOCK_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
    add_test(NAME hammock_tests COMMAND hammock_tests)

    # Смоук-тест утилиты командной строки на примере из tests/data.
    add_test(NAME cli_example COMMAND hammocks ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/if_then_else.graphml)
    set_tests_properties(cli_example PROPERTIES PASS_REGULAR_EXPRESSION "Найдено гамаков: 8")
    add_test(NAME cli_bad_file COMMAND hammocks ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/undirected.graphml)
    set_tests_properties(cli_bad_file PROPERTIES WILL_FAIL TRUE)
endif()
