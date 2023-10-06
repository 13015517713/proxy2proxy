cmake_minimum_required(VERSION 3.25)
project(proxy2proxy_mine_latest)

set(srcs
    main.cpp
    io.cpp
    http.cpp
)

add_executable(main ${srcs})

set(CMAKE_GENERATOR_PLATFORM x64)

set_target_properties(main PROPERTIES
    CXX_STANDARD 11
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

target_link_libraries(main pthread)
