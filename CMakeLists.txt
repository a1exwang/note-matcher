cmake_minimum_required(VERSION 3.0)

set(CMAKE_CXX_STANDARD 20)
project(note-matcher)

find_package(SDL2 REQUIRED)

add_executable(note_matcher main.cpp)
target_link_libraries(note_matcher PRIVATE SDL2 SDL2_ttf)
