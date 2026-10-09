# cmake_minimum_required(VERSION 3.20)
# //project(mep03_hello_pi LANGUAGES CXX)
# project(bt_chat_server CXX)
# # set(CMAKE_CXX_STANDARD 14)
# # set(CMAKE_CXX_STANDARD_REQUIRED ON)
# # set(CMAKE_CXX_EXTENSIONS OFF)

# add_executable(hello_pi src/00_hello_pi.cc)

cmake_minimum_required(VERSION 3.10)
project(bt_chat_server CXX)

set(CMAKE_CXX_STANDARD 17)

add_executable(bt_chat_server src/main.cpp)

# BlueZ 블루투스 라이브러리 및 멀티스레드 라이브러리 링킹
target_link_libraries(bt_chat_server PRIVATE bluetooth pthread)