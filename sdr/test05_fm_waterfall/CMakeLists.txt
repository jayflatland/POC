cmake_minimum_required(VERSION 3.28.3)
project(fm_waterfall CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Boost REQUIRED COMPONENTS program_options)
find_package(OpenGL REQUIRED)
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFTW3F REQUIRED fftw3f)

add_executable(fm_waterfall fm_waterfall.cpp)
target_include_directories(fm_waterfall PRIVATE ${FFTW3F_INCLUDE_DIRS})
target_link_libraries(fm_waterfall PRIVATE uhd Boost::program_options OpenGL::GL glut ${FFTW3F_LIBRARIES} Threads::Threads)
