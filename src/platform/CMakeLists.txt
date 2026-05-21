add_library(platform STATIC
    error.cpp
    window.cpp
    gfx.cpp
    text.cpp
    shell.cpp
)

target_include_directories(platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(platform PUBLIC cxx_std_23)

target_link_libraries(platform PUBLIC
    d3d11
    dxgi
    dwrite
    d3dcompiler
    user32
    gdi32
)
