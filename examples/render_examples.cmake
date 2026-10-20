# Рисует каждый examples/*.graphml в двух вариантах:
#   svg/<имя>.svg       — укладка на основе гамаков;
#   svg/<имя>_flat.svg  — обычная укладка Сугиямы для сравнения.
# Запуск: cmake -DLAYOUT=<путь к hammock_layout> -DEXAMPLES=<папка examples> -P render_examples.cmake
file(MAKE_DIRECTORY "${EXAMPLES}/svg")
file(GLOB graphs "${EXAMPLES}/*.graphml")
foreach(graph IN LISTS graphs)
    get_filename_component(name "${graph}" NAME_WE)
    execute_process(COMMAND "${LAYOUT}" "${graph}" -o "${EXAMPLES}/svg/${name}.svg" --title "${name}: гамаки"
                    RESULT_VARIABLE rc1)
    execute_process(COMMAND "${LAYOUT}" "${graph}" -o "${EXAMPLES}/svg/${name}_flat.svg" --flat
                            --title "${name}: без гамаков"
                    RESULT_VARIABLE rc2)
    if(NOT rc1 EQUAL 0 OR NOT rc2 EQUAL 0)
        message(FATAL_ERROR "не удалось нарисовать ${graph}")
    endif()
    message(STATUS "${name}")
endforeach()
