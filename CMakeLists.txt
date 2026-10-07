cmake_minimum_required(VERSION 3.20)
project(mep03_hello_pi LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(hello_pi src/00_hello_pi.cc)