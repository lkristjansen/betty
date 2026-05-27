# tomlplusplus — header-only TOML parser
include(FetchContent)
FetchContent_Declare(
    tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus
    GIT_TAG        v3.4.0
)
FetchContent_MakeAvailable(tomlplusplus)

add_library(config STATIC
    config.cpp
)

target_include_directories(config PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(config PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

target_link_libraries(config PUBLIC
    tomlplusplus::tomlplusplus
)

target_compile_features(config PUBLIC cxx_std_23)

target_compile_options(config PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/W4;/WX;/utf-8>
  $<$<CXX_COMPILER_ID:Clang>:-Wall;-Wextra;-Werror>
)
