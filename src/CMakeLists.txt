add_subdirectory(platform)
add_subdirectory(terminal)

add_executable(betty main.cpp app.cpp util/log.cpp)
target_include_directories(betty PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(betty PRIVATE platform terminal)
target_compile_features(betty PRIVATE cxx_std_23)

target_compile_options(betty PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/W4;/WX;/utf-8>
  $<$<CXX_COMPILER_ID:Clang>:-Wall;-Wextra;-Werror>
)
set_target_properties(betty PROPERTIES
    WIN32_EXECUTABLE TRUE
)

# Use main() as entry point even with WINDOWS subsystem (no console window).
target_link_options(betty PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/ENTRY:mainCRTStartup>
  $<$<CXX_COMPILER_ID:Clang>:-Wl,/entry:mainCRTStartup>
)

# Install
install(TARGETS betty RUNTIME DESTINATION .)
