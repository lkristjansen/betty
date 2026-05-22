add_library(terminal STATIC
    grid.cpp
    input_handler.cpp
    vt_parser.cpp
)

target_include_directories(terminal PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# So terminal files can #include "platform/types.hpp"
target_include_directories(terminal PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(terminal PUBLIC cxx_std_23)

if(MSVC)
    target_compile_options(terminal PRIVATE /W4 /WX /utf-8)
endif()
