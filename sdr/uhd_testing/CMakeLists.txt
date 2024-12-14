find_package(Boost REQUIRED COMPONENTS filesystem program_options)
include_directories(${Boost_INCLUDE_DIRS})

add_executable(uhd_test01 uhd_test01.cpp)
target_link_libraries(uhd_test01 uhd ${Boost_LIBRARIES})

add_executable(uhd_test02 uhd_test02.cpp)
target_link_libraries(uhd_test02 uhd ${Boost_LIBRARIES})
