cmake_minimum_required(VERSION 3.28.3)
find_package(Boost REQUIRED COMPONENTS filesystem program_options)

add_executable(record_to_cfile record_to_cfile.cpp)
target_link_libraries(record_to_cfile PRIVATE uhd)
