cmake_minimum_required(VERSION 3.16)

find_package(PkgConfig)

set(CMAKE_CXX_STANDARD 20)
#set(CMAKE_CXX_STANDARD_REQUIRED ON)
#set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS "-fconcepts -Werror -Wshadow -Wall -Wno-terminate -Wno-sign-compare -Wno-unused-result -Wno-format-truncation -fverbose-asm -Wno-deprecated-declarations -mavx2 -DBOOST_BIND_GLOBAL_PLACEHOLDERS -DBOOST_BEAST_USE_STD_STRING_VIEW")
set(CMAKE_CXX_FLAGS_DEBUG "-g")
set(CMAKE_CXX_FLAGS_RELEASE "-g -O3 -DNDEBUG")

project (wordle)
find_package(Boost COMPONENTS system program_options)

include_directories(${Boost_INCLUDE_DIR})

add_executable(optimizer optimizer.cpp)
target_link_libraries(optimizer ${Boost_LIBRARIES} fmt)

# add_executable(screen_read_tester screen_read_tester.cpp)
# target_link_libraries(screen_read_tester ${Boost_LIBRARIES} X11)





