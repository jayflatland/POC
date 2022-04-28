if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS "-Wall -Wextra")
set(CMAKE_CXX_FLAGS_DEBUG "-g")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

project (optimizer)
find_package(Boost COMPONENTS system)
include_directories(${Boost_INCLUDE_DIR})
add_executable(optimizer optimizer.cpp)
target_link_libraries(optimizer ${Boost_LIBRARIES})



