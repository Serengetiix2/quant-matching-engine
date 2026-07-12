cmake_minimum_required(VERSION 3.10)
project(matching_engine)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(tests tests.cpp)
# add_executable(engine main.cpp)   # uncomment if you want to keep main.cpp around