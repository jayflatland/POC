cmake_minimum_required(VERSION 3.16)

# macOS: an Intel Homebrew (/usr/local, even under Rosetta on Apple Silicon)
# only has x86_64 libraries, so build x86_64 to match
if(CMAKE_HOST_APPLE AND NOT CMAKE_OSX_ARCHITECTURES AND EXISTS /usr/local/bin/brew
   AND NOT EXISTS /opt/homebrew/bin/brew)
    set(CMAKE_OSX_ARCHITECTURES x86_64 CACHE STRING "")
endif()

project(wifi_flow CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# -march=native isn't accepted by older clang on Apple Silicon; -mcpu=native is.
# Neither applies when building x86_64 on an arm64 Mac (Rosetta).
include(CheckCXXCompilerFlag)
set(CMAKE_CXX_FLAGS_RELEASE "-O3")
if(NOT CMAKE_OSX_ARCHITECTURES OR CMAKE_OSX_ARCHITECTURES STREQUAL CMAKE_HOST_SYSTEM_PROCESSOR)
    check_cxx_compiler_flag("-march=native" HAVE_MARCH_NATIVE)
    check_cxx_compiler_flag("-mcpu=native" HAVE_MCPU_NATIVE)
    if(HAVE_MARCH_NATIVE)
        set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native")
    elseif(HAVE_MCPU_NATIVE)
        set(CMAKE_CXX_FLAGS_RELEASE "-O3 -mcpu=native")
    endif()
endif()

# macOS: Homebrew's qt@5 is keg-only, so point CMake at it, and use
# Homebrew's pkg-config rather than whichever (MacPorts, conda) is first on PATH
if(APPLE)
    find_program(BREW brew)
    if(BREW)
        get_filename_component(BREW_BIN ${BREW} DIRECTORY)
        if(NOT PKG_CONFIG_EXECUTABLE AND EXISTS ${BREW_BIN}/pkg-config)
            set(PKG_CONFIG_EXECUTABLE ${BREW_BIN}/pkg-config CACHE FILEPATH "")
        endif()
        execute_process(COMMAND ${BREW} --prefix qt@5 OUTPUT_VARIABLE QT5_PREFIX
                        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        if(QT5_PREFIX)
            list(APPEND CMAKE_PREFIX_PATH ${QT5_PREFIX})
        endif()
    endif()
endif()

find_package(Boost REQUIRED COMPONENTS program_options)
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFTW3F REQUIRED fftw3f)

# UHD ships a CMake config; fall back to the bare library name (Linux system install)
find_package(UHD QUIET)
if(UHD_FOUND)
    set(UHD_LIBS ${UHD_LIBRARIES})
else()
    set(UHD_LIBS uhd)
endif()

# WiFi PHY decoders live in test07
set(FLOW_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../test07_wifi_ssid ${FFTW3F_INCLUDE_DIRS} ${UHD_INCLUDE_DIRS})
set(FLOW_LIBS ${UHD_LIBS} Boost::program_options ${FFTW3F_LINK_LIBRARIES} Threads::Threads)

add_executable(wifi_flow_cli wifi_flow_cli.cpp)
target_include_directories(wifi_flow_cli PRIVATE ${FLOW_INCLUDES})
target_link_libraries(wifi_flow_cli PRIVATE ${FLOW_LIBS})

find_package(Qt5 COMPONENTS Widgets QUIET)
if(Qt5Widgets_FOUND)
    add_executable(wifi_flow wifi_flow.cpp)
    target_include_directories(wifi_flow PRIVATE ${FLOW_INCLUDES})
    target_link_libraries(wifi_flow PRIVATE ${FLOW_LIBS} Qt5::Widgets)
    target_compile_definitions(wifi_flow PRIVATE QT_NO_KEYWORDS)
else()
    message(STATUS "Qt5 not found: building only wifi_flow_cli (apt install qtbase5-dev / brew install qt@5 for the GUI)")
endif()
