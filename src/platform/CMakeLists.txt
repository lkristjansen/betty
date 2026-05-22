add_library(platform STATIC
    error.cpp
    window.cpp
    gfx.cpp
    text.cpp
    shell.cpp
)

target_include_directories(platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(platform PUBLIC cxx_std_23)

if(MSVC)
    target_compile_options(platform PRIVATE /W4 /WX /utf-8)
endif()

target_link_libraries(platform PRIVATE
    d3d11
    dxgi
    dwrite
    d3dcompiler
    user32
    gdi32
)
