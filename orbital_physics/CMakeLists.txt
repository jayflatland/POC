set(Boost_USE_STATIC_LIBS   ON)
find_package(Boost COMPONENTS system)
include_directories(${Boost_INCLUDE_DIR})

add_executable(orbital_physics orbital_physics.cpp)
target_link_libraries(orbital_physics glut GL)
