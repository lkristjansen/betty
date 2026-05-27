add_executable(betty_tests
    cursor_state_test.cpp
    sgr_state_test.cpp
    scrollback_buffer_test.cpp
    vt_parser_test.cpp
    grid_test.cpp
    input_handler_test.cpp
    utf8_test.cpp
    shell_test.cpp
    config_test.cpp
)

target_link_libraries(betty_tests PRIVATE
    terminal
    platform
    config
    Catch2::Catch2WithMain
)

target_compile_features(betty_tests PRIVATE cxx_std_23)

target_compile_options(betty_tests PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/W4;/WX;/utf-8>
  $<$<CXX_COMPILER_ID:Clang>:-Wall;-Wextra;-Werror>
)

add_test(test betty_tests)
