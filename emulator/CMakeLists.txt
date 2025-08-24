cmake_minimum_required(VERSION 3.10)
project(emu8086 C)

# Collect all .c sources from src/
file(GLOB_RECURSE ALL_SOURCES "${CMAKE_SOURCE_DIR}/src/*.c")

# Include headers
include_directories("${CMAKE_SOURCE_DIR}/include")

# -------------------
# Build emu8086
# -------------------
set(EMU_SOURCES ${ALL_SOURCES})
list(REMOVE_ITEM EMU_SOURCES "${CMAKE_SOURCE_DIR}/src/server.c")

if(WIN32)
    add_executable(emu8086 WIN32 ${EMU_SOURCES})
else()
    add_executable(emu8086 ${EMU_SOURCES})
endif()

# -------------------
# Build emu_server
# -------------------
set(SERVER_SOURCES ${ALL_SOURCES})
list(REMOVE_ITEM SERVER_SOURCES "${CMAKE_SOURCE_DIR}/src/server.c")
list(REMOVE_ITEM SERVER_SOURCES "${CMAKE_SOURCE_DIR}/src/main.c")

if(WIN32)
    add_executable(emu_server WIN32 "${CMAKE_SOURCE_DIR}/src/server.c" ${SERVER_SOURCES})
else()
    add_executable(emu_server "${CMAKE_SOURCE_DIR}/src/server.c" ${SERVER_SOURCES})
endif()

# Link ws2_32 only on Windows
if(WIN32)
    target_link_libraries(emu_server PRIVATE ws2_32)
endif()