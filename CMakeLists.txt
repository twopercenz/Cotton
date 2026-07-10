cmake_minimum_required(VERSION 3.10)
project(cotton)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(src)

add_executable(cotton
    src/main.cpp
    src/lexer.cpp
    src/parser.cpp
    src/value.cpp
    src/interpreter.cpp
)

# Enable warnings
if (MSVC)
    target_compile_options(cotton PRIVATE /W4)
else()
    target_compile_options(cotton PRIVATE -Wall -Wextra -g)
endif()
