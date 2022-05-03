cmake_minimum_required(VERSION 3.16)

find_package(PkgConfig)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS "-Wall -Wextra")
set(CMAKE_CXX_FLAGS_DEBUG "-g")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

project (wordle)
find_package(Boost COMPONENTS system program_options)

include_directories(${Boost_INCLUDE_DIR})

add_executable(optimizer optimizer.cpp)
target_link_libraries(optimizer ${Boost_LIBRARIES})

# add_executable(screen_read_tester screen_read_tester.cpp)
# target_link_libraries(screen_read_tester ${Boost_LIBRARIES} X11)





