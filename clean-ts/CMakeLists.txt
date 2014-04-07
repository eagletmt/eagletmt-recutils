project(clean-ts)
cmake_minimum_required(VERSION 2.8)

include(FindPkgConfig)
pkg_check_modules(FFMPEG REQUIRED libavcodec libavformat libavutil)
include_directories(${FFMPEG_INCLUDE_DIRS})
link_directories(${FFMPEG_LIBRARY_DIRS})
link_libraries(${FFMPEG_LIBRARIES})

add_definitions(-Wall -W)
set(CMAKE_C_FLAGS_RELEASE "-O3")
set(CMAKE_C_FLAGS_DEBUG "-Og -g")

add_executable(clean-ts clean-ts.c)

install(PROGRAMS clean-ts DESTINATION bin)
