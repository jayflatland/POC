cmake_minimum_required(VERSION 3.28.3)
# project(uhd)
find_package(Boost REQUIRED COMPONENTS filesystem program_options)
# include_directories(${Boost_INCLUDE_DIRS})

find_package(OpenGL REQUIRED)

# add_executable(uhd_test01 uhd_test01.cpp)
# target_link_libraries(uhd_test01 uhd ${Boost_LIBRARIES})

# add_executable(uhd_test02 uhd_test02.cpp)
# target_link_libraries(uhd_test02 uhd ${Boost_LIBRARIES})

# include_directories( ${OPENGL_INCLUDE_DIRS}  ${GLUT_INCLUDE_DIRS} )
# add_executable(uhd_test03 uhd_test03.cpp)
# target_link_libraries(uhd_test03 PRIVATE ${OPENGL_LIBRARIES} glut uhd)

# find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGL)
# # include_directories( ${OPENGL_INCLUDE_DIRS}  ${GLUT_INCLUDE_DIRS} )
# add_executable(uhd_test04 uhd_test04/uhd_test04.cpp)
# target_link_libraries(uhd_test04 ${OPENGL_LIBRARIES} glut uhd)

find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets OpenGL)
add_executable(${PROJECT_NAME} uhd_test04/uhd_test04.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE ${OPENGL_LIBRARIES} Qt5::Core Qt5::Gui Qt5::Widgets Qt5::OpenGL uhd)
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(${PROJECT_NAME} PROPERTIES AUTOMOC ON)
