set(Boost_USE_STATIC_LIBS   ON)
find_package(Boost COMPONENTS system)
include_directories(${Boost_INCLUDE_DIR})

set(SOURCES
    particle_stuff.cpp
    particle.cpp
    vector.cpp
)

add_executable(particle_stuff ${SOURCES})
target_link_libraries(particle_stuff glut GL)
