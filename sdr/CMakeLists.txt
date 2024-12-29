cmake_minimum_required(VERSION 3.28.3)
project(uhd_gui_testing VERSION 1.0)
find_package(Boost REQUIRED COMPONENTS filesystem program_options)
find_package(OpenGL REQUIRED)
find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets OpenGL)


add_executable(${PROJECT_NAME} uhd_gui_testing.cpp)
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
# # set_target_properties(${PROJECT_NAME} PROPERTIES AUTOMOC ON)
target_link_libraries(${PROJECT_NAME} PRIVATE ${OPENGL_LIBRARIES} Qt5::Core Qt5::Gui Qt5::Widgets Qt5::OpenGL uhd)
# # target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
