cmake_minimum_required(VERSION 3.25)
project(proxy2proxy_mine_latest)

# set(Boost_USE_STATIC_LIBS ON)
set(Boost_USE_STATIC_RUNTIME ON)

find_package(Boost 1.55 COMPONENTS thread system filesystem program_options random atomic chrono)
if(NOT Boost_FOUND)
    message(FATAL_ERROR "Boost not found. Please check BOOST_ROOT variable.")
endif()

set(srcs
    main.cpp
    io.cpp
    http.cpp
)
add_executable(main ${srcs})

set_target_properties(main PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

#include_directories(${Boost_INCLUDE_DIRS})
#link_directories(${Boost_LIBRARY_DIRS})
target_link_libraries(main ${Boost_LIBRARIES})