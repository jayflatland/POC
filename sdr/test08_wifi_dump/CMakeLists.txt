cmake_minimum_required(VERSION 3.28.3)
project(wifi_dump CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Boost REQUIRED COMPONENTS program_options)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFTW3F REQUIRED fftw3f)

# OFDM / DSSS / MAC decoders live in test07
add_executable(wifi_dump wifi_dump.cpp)
target_include_directories(wifi_dump PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../test07_wifi_ssid ${FFTW3F_INCLUDE_DIRS})
target_link_libraries(wifi_dump PRIVATE uhd Boost::program_options ${FFTW3F_LIBRARIES})
