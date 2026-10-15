cmake_minimum_required(VERSION 3.28.3)
project(tx_scanner CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Boost REQUIRED COMPONENTS program_options)
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFTW3F REQUIRED fftw3f)

add_executable(tx_scanner tx_scanner.cpp)
target_include_directories(tx_scanner PRIVATE ${FFTW3F_INCLUDE_DIRS})
target_link_libraries(tx_scanner PRIVATE uhd Boost::program_options ${FFTW3F_LIBRARIES} Threads::Threads)
