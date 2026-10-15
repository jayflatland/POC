cmake_minimum_required(VERSION 3.28.3)
project(sdrshark CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native")

find_package(Boost REQUIRED COMPONENTS program_options)
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFTW3F REQUIRED fftw3f)

# WiFi PHY decoders live in test07
set(SHARK_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../test07_wifi_ssid ${FFTW3F_INCLUDE_DIRS})
set(SHARK_LIBS uhd Boost::program_options ${FFTW3F_LIBRARIES} Threads::Threads)

add_executable(sdrshark_cli sdrshark_cli.cpp)
target_include_directories(sdrshark_cli PRIVATE ${SHARK_INCLUDES})
target_link_libraries(sdrshark_cli PRIVATE ${SHARK_LIBS})

find_package(Qt5 COMPONENTS Widgets QUIET)
if(Qt5Widgets_FOUND)
    add_executable(sdrshark sdrshark_gui.cpp)
    target_include_directories(sdrshark PRIVATE ${SHARK_INCLUDES})
    target_link_libraries(sdrshark PRIVATE ${SHARK_LIBS} Qt5::Widgets)
    # Qt's 'emit' keyword macro collides with the decoders' emit callbacks
    target_compile_definitions(sdrshark PRIVATE QT_NO_KEYWORDS)
else()
    message(STATUS "Qt5 not found: building only sdrshark_cli (sudo apt install qtbase5-dev for the GUI)")
endif()
