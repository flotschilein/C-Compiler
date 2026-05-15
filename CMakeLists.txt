cmake_minimum_required(VERSION 3.10)
project(C_Compiler)

set(CMAKE_CXX_STANDARD 17)

add_executable(C_Compiler
        main.cpp
        preprocessor.cpp
        preprocessor.h
        lexer.cpp
        lexer.h
        token.cpp
        token.h
        ast.cpp
        ast.h
        parser.cpp
        parser.h
        semantic.cpp
        semantic.h
        ir_type.h ir_type.cpp
        ir.h ir.cpp
        ir_printer.h ir_printer.cpp
        ir_builder.h ir_builder.cpp
        ir_instantiator.h ir_instantiator.cpp)
