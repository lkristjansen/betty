cmake_minimum_required(VERSION 3.28)
project(betty LANGUAGES CXX)

option(BUILD_TESTING "Build unit tests" OFF)

add_subdirectory(src)

if(BUILD_TESTING)
    include(FetchContent)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.12.0
    )
    FetchContent_MakeAvailable(Catch2)
    
    include(CTest)
    add_subdirectory(tests)
endif()

# CPack
set(CPACK_PACKAGE_NAME "betty")
set(CPACK_PACKAGE_VERSION "0.1.0")
set(CPACK_PACKAGE_DESCRIPTION "A GPU-accelerated terminal emulator")
set(CPACK_GENERATOR "ZIP")
include(CPack)
