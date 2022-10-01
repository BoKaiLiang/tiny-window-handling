cmake_minimum_required(VERSION 3.10)
project(twh-example LANGUAGES C)

# Headers and sources

set(HEADERS
    twh.h
)
set(SOURCES
    twh_example.c
)

if(WIN32)
    set(SOURCES ${SOURCES} twh_win32.c)
else()
    set(SOURCES ${SOURCES} twh_linux.c)
endif()

# Target definition

set(TARGET twh-example)

add_executable(${TARGET} ${HEADERS} ${SOURCES})


# Target properties

set_target_properties(${TARGET} PROPERTIES C_STANDARD 11)
set_target_properties(${TARGET} PROPERTIES C_EXTENSIONS OFF)
set_target_properties(${TARGET} PROPERTIES INTERPROCEDURAL_OPTIMIZATION ON)

# Compile options

if(MSVC)
    target_compile_options(${TARGET} PRIVATE /W4 /D_CRT_SECURE_NO_WARNINGS)
    target_compile_options(${TARGET} PRIVATE /fp:fast)
else()
    target_compile_options(${TARGET} PRIVATE -Wall -Wextra -pedantic)
    target_compile_options(${TARGET} PRIVATE -ffast-math)
endif()

if(UNIX AND NOT APPLE)
    target_compile_options(${TARGET} PRIVATE -D_POSIX_C_SOURCE=200809L)
endif()

# Link libraries

if(WIN32)
    # nothing to do for now
else()
    target_link_libraries(${TARGET} PRIVATE m X11)
endif()