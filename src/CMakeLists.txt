add_subdirectory(platform)

add_executable(betty main.cpp)
target_link_libraries(betty PRIVATE platform)
target_compile_features(betty PRIVATE cxx_std_23)
set_target_properties(betty PROPERTIES
    WIN32_EXECUTABLE TRUE
)

# Use main() as entry point even with WINDOWS subsystem (no console window).
if(MSVC)
    target_link_options(betty PRIVATE /ENTRY:mainCRTStartup)
endif()

# Install
install(TARGETS betty RUNTIME DESTINATION .)
