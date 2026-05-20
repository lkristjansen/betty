cmake_minimum_required(VERSION 3.28)
project(betty LANGUAGES CXX)

add_subdirectory(src)

# CPack
set(CPACK_PACKAGE_NAME "betty")
set(CPACK_PACKAGE_VERSION "0.1.0")
set(CPACK_PACKAGE_DESCRIPTION "A GPU-accelerated terminal emulator")
set(CPACK_GENERATOR "ZIP")
include(CPack)
