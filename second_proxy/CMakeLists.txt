cmake_minimum_required(VERSION 3.25)
project(proxy2proxy_mine_latest_second_proxy)

# 设置MinGW编译器的路径
set(CMAKE_MAKE_PROGRAM "C:/MinGW/msys/1.0/bin/make.exe")
set(CMAKE_C_COMPILER "C:/MinGW/bin/gcc.exe")
set(CMAKE_CXX_COMPILER "C:/MinGW/bin/g++.exe")

# 其他CMake配置和项目设置
set(CMAKE_CXX_STANDARD 11)
set(srcs
    main.cpp
)
add_executable(main ${srcs})

