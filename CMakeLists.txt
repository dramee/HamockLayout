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
    src/hammock_tree.cpp
    src/sugiyama.cpp
    src/layout.cpp
    src/svg.cpp
)
target_include_directories(hammock PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(hammocks src/main.cpp)
target_link_libraries(hammocks PRIVATE hammock)

add_executable(hammock_layout src/layout_main.cpp)
target_link_libraries(hammock_layout PRIVATE hammock)
if(WIN32)
    target_link_libraries(hammocks PRIVATE shell32)
    target_link_libraries(hammock_layout PRIVATE shell32)
endif()

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
        tests/test_hammock_tree.cpp
        tests/test_sugiyama.cpp
        tests/test_layout.cpp
    )
    target_link_libraries(hammock_tests PRIVATE hammock)
    target_compile_definitions(hammock_tests PRIVATE
        HAMMOCK_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data"
        HAMMOCK_EXAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/examples")
    add_test(NAME hammock_tests COMMAND hammock_tests)

    # Смоук-тест утилиты командной строки на примере из tests/data.
    add_test(NAME cli_example COMMAND hammocks ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/if_then_else.graphml)
    set_tests_properties(cli_example PROPERTIES PASS_REGULAR_EXPRESSION "Найдено гамаков: 8")
    add_test(NAME cli_bad_file COMMAND hammocks ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/undirected.graphml)
    set_tests_properties(cli_bad_file PROPERTIES WILL_FAIL TRUE)

    # Смоук-тесты утилиты визуализации.
    add_test(NAME layout_cli_example
        COMMAND hammock_layout ${CMAKE_CURRENT_SOURCE_DIR}/examples/04_bubble_sort.graphml --title test)
    set_tests_properties(layout_cli_example PROPERTIES PASS_REGULAR_EXPRESSION "class=\"hammock\".*class=\"vertex\"")
    add_test(NAME layout_cli_bad_file
        COMMAND hammock_layout ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/undirected.graphml)
    set_tests_properties(layout_cli_bad_file PROPERTIES WILL_FAIL TRUE)
endif()

# Перерисовка примеров: cmake --build build --target examples
add_custom_target(examples
    COMMAND ${CMAKE_COMMAND} -DLAYOUT=$<TARGET_FILE:hammock_layout>
            -DEXAMPLES=${CMAKE_CURRENT_SOURCE_DIR}/examples
            -P ${CMAKE_CURRENT_SOURCE_DIR}/examples/render_examples.cmake
    DEPENDS hammock_layout
    COMMENT "Рисую примеры в examples/svg")
