set(Boost_USE_STATIC_LIBS   ON)
find_package(Boost COMPONENTS system)
include_directories(${Boost_INCLUDE_DIR})

set(SOURCES
    orbital_physics.cpp
    particle.cpp
    vector.cpp
)

add_executable(orbital_physics ${SOURCES})
target_link_libraries(orbital_physics glut GL)
