project(clean-ts)
cmake_minimum_required(VERSION 2.8)

include(FindPkgConfig)
pkg_check_modules(FFMPEG REQUIRED libavcodec libavformat libavutil)
include_directories(${FFMPEG_INCLUDE_DIRS})
link_directories(${FFMPEG_LIBRARY_DIRS})
link_libraries(${FFMPEG_LIBRARIES})

add_definitions(-Wall -W)
set(CMAKE_C_FLAGS_RELEASE "-O3")

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  set(CMAKE_C_FLAGS_DEBUG "-O1 -g -DCLEAN_TS_DEBUG")
else()
  set(CMAKE_C_FLAGS_DEBUG "-Og -g -DCLEAN_TS_DEBUG")
endif()

add_executable(clean-ts clean-ts.c)

install(PROGRAMS clean-ts DESTINATION bin)
