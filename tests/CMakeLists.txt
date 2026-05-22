add_executable(betty_tests
    vt_parser_test.cpp
    grid_test.cpp
    input_handler_test.cpp
)

target_link_libraries(betty_tests PRIVATE
    terminal
    Catch2::Catch2WithMain
)

target_compile_features(betty_tests PRIVATE cxx_std_23)

if(MSVC)
    target_compile_options(betty_tests PRIVATE /W4 /WX /utf-8)
endif()

add_test(test betty_tests)
