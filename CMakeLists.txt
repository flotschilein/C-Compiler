cmake_minimum_required(VERSION 4.0)
project(C_Compiler)

set(CMAKE_CXX_STANDARD 17)

add_executable(C_Compiler
        main.cpp
        preprocessor.cpp
        preprocessor.h)
