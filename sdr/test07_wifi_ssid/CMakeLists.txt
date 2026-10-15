cmake_minimum_required(VERSION 3.28.3)
project(wifi_scan CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Boost REQUIRED COMPONENTS program_options)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFTW3F REQUIRED fftw3f)

add_executable(wifi_scan wifi_scan.cpp)
target_include_directories(wifi_scan PRIVATE ${FFTW3F_INCLUDE_DIRS})
target_link_libraries(wifi_scan PRIVATE uhd Boost::program_options ${FFTW3F_LIBRARIES})

add_executable(phy_selftest phy_selftest.cpp)
target_include_directories(phy_selftest PRIVATE ${FFTW3F_INCLUDE_DIRS})
target_link_libraries(phy_selftest PRIVATE ${FFTW3F_LIBRARIES})
