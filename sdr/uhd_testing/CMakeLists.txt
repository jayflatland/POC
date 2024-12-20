cmake_minimum_required(VERSION 3.28.3)
project(uhd)
find_package(Boost REQUIRED COMPONENTS filesystem program_options)
include_directories(${Boost_INCLUDE_DIRS})

find_package(OpenGL REQUIRED)

add_executable(uhd_test01 uhd_test01.cpp)
target_link_libraries(uhd_test01 uhd ${Boost_LIBRARIES})

add_executable(uhd_test02 uhd_test02.cpp)
target_link_libraries(uhd_test02 uhd ${Boost_LIBRARIES})

include_directories( ${OPENGL_INCLUDE_DIRS}  ${GLUT_INCLUDE_DIRS} )
add_executable(uhd_test03 uhd_test03.cpp)
target_link_libraries(uhd_test03 ${OPENGL_LIBRARIES} glut uhd)
