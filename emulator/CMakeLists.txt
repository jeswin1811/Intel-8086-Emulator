cmake_minimum_required(VERSION 3.10)
project(emu8086 C)

file(GLOB_RECURSE SOURCES "src/*.c")
include_directories(include)

add_executable(emu8086 ${SOURCES})
