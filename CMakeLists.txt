cmake_minimum_required(VERSION 3.14)
project(ngram C)

set(CMAKE_C_STANDARD 11)

option(NGRAM_NATIVE "Enable native CPU optimizations" OFF)

if(NOT MSVC)
    add_compile_options(-O3 -Wall -Wextra -Werror -fno-finite-math-only)
    add_definitions(-D_GNU_SOURCE)
    if(NGRAM_NATIVE)
        add_compile_options(-march=native)
    endif()
else()
    add_compile_options(/O2 /W3 /WX)
endif()

set(NGRAM_SOURCES src/libngram.c)
set(NGRAM_HEADERS src/ngram.h)

add_library(ngram_shared SHARED ${NGRAM_SOURCES})
set_target_properties(ngram_shared PROPERTIES OUTPUT_NAME ngram PREFIX "lib")
target_include_directories(ngram_shared PRIVATE src)

add_library(ngram_static STATIC ${NGRAM_SOURCES})
set_target_properties(ngram_static PROPERTIES OUTPUT_NAME ngram PREFIX "lib" POSITION_INDEPENDENT_CODE ON)
target_include_directories(ngram_static PRIVATE src)

add_executable(ngram_exe src/ngram.c ${NGRAM_SOURCES})
set_target_properties(ngram_exe PROPERTIES OUTPUT_NAME ngram)
target_include_directories(ngram_exe PRIVATE src)
