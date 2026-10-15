cmake_minimum_required(VERSION 3.28.3)
project(airband CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Boost REQUIRED COMPONENTS program_options)
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFTW3F REQUIRED fftw3f)

add_executable(airband airband.cpp)
target_include_directories(airband PRIVATE ${FFTW3F_INCLUDE_DIRS})
target_link_libraries(airband PRIVATE uhd Boost::program_options ${FFTW3F_LIBRARIES} Threads::Threads)
