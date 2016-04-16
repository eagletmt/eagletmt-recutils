project(assdumper)
cmake_minimum_required(VERSION 2.8)

include(FindPkgConfig)
pkg_check_modules(LIBS REQUIRED libdvbpsi aribb24)
include_directories(${LIBS_INCLUDE_DIRS})
link_directories(${LIBS_LIBRARY_DIRS})
link_libraries(${LIBS_LIBRARIES})

add_definitions(-std=c++11 -Wall -W)
set(CMAKE_CXX_FLAGS_RELEASE "-O3")
set(CMAKE_CXX_FLAGS_DEBUG "-Og -ggdb3")

add_executable(assdumper assdumper.cc)

install(PROGRAMS assdumper DESTINATION bin)
