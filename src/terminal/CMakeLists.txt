add_library(terminal STATIC
    text_buffer.cpp
    input_handler.cpp
)

target_include_directories(terminal PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# So terminal files can #include "platform/types.hpp"
target_include_directories(terminal PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(terminal PUBLIC cxx_std_23)
