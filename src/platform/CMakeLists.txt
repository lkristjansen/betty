add_library(platform STATIC
    error.cpp
    window.cpp
    gfx.cpp
    text.cpp
    shell.cpp
    unicode.cpp
)

target_include_directories(platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# So platform files can #include "terminal/types.hpp"
target_include_directories(platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_definitions(platform PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_features(platform PUBLIC cxx_std_23)

target_compile_options(platform PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/W4;/WX;/utf-8>
  $<$<CXX_COMPILER_ID:Clang>:-Wall;-Wextra;-Werror>
)

target_link_libraries(platform PRIVATE
    d3d11
    dxgi
    dwrite
    d3dcompiler
    user32
    gdi32
    Normaliz
)
