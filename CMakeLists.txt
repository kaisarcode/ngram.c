cmake_minimum_required(VERSION 3.14)
project(ngram C)

set(CMAKE_C_STANDARD 11)
set(KC_NGRAM_BUILD_VERSION "0" CACHE STRING "ngram build version timestamp")

option(NGRAM_NATIVE "Enable native CPU optimizations" OFF)
option(NGRAM_BUILD_TESTS "Build ngram tests" OFF)

set(NGRAM_TEST_EXTERNAL_ARTIFACTS OFF)
if(NGRAM_BUILD_TESTS AND NGRAM_TEST_SHARED_LIBRARY)
    set(NGRAM_TEST_EXTERNAL_ARTIFACTS ON)
endif()

if(NOT MSVC)
    add_compile_options(-O3 -Wall -Wextra -Werror -fno-finite-math-only)
    if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        add_compile_options(-Wno-unused-command-line-argument)
    endif()
    add_definitions(-D_GNU_SOURCE)
    if(NGRAM_NATIVE)
        add_compile_options(-march=native)
    endif()
else()
    add_compile_options(/O2 /W3 /WX)
endif()

set(NGRAM_SOURCES src/libngram.c)
set(NGRAM_HEADERS src/ngram.h)

set_property(
    SOURCE src/libngram.c
    APPEND PROPERTY COMPILE_DEFINITIONS KC_NGRAM_BUILD_VERSION=${KC_NGRAM_BUILD_VERSION}
)

if(NOT NGRAM_TEST_EXTERNAL_ARTIFACTS)
    add_library(ngram_shared SHARED ${NGRAM_SOURCES})
    set_target_properties(ngram_shared PROPERTIES OUTPUT_NAME ngram PREFIX "lib")
    target_include_directories(ngram_shared PRIVATE src)

    add_library(ngram_static STATIC ${NGRAM_SOURCES})
    set_target_properties(ngram_static PROPERTIES OUTPUT_NAME ngram PREFIX "lib" POSITION_INDEPENDENT_CODE ON)
    target_include_directories(ngram_static PRIVATE src)

    add_executable(ngram_exe src/ngram.c ${NGRAM_SOURCES})
    set_target_properties(ngram_exe PROPERTIES OUTPUT_NAME ngram)
    target_include_directories(ngram_exe PRIVATE src)
endif()

if(NGRAM_BUILD_TESTS)
    enable_testing()

    if(NGRAM_TEST_SHARED_LIBRARY)
        add_library(ngram_test_shared SHARED IMPORTED)
        set_target_properties(ngram_test_shared PROPERTIES
            IMPORTED_LOCATION "${NGRAM_TEST_SHARED_LIBRARY}"
        )
        if(NGRAM_TEST_IMPORT_LIBRARY)
            set_target_properties(ngram_test_shared PROPERTIES
                IMPORTED_IMPLIB "${NGRAM_TEST_IMPORT_LIBRARY}"
            )
        endif()
    else()
        set(NGRAM_TEST_SHARED_TARGET ngram_shared)
    endif()

    if(TARGET ngram_test_shared)
        set(NGRAM_TEST_SHARED_TARGET ngram_test_shared)
    endif()

    add_executable(ngram_contract_test src/test.c)
    target_include_directories(ngram_contract_test PRIVATE src)
    target_link_libraries(ngram_contract_test PRIVATE ${NGRAM_TEST_SHARED_TARGET})

    add_custom_command(TARGET ngram_contract_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:${NGRAM_TEST_SHARED_TARGET}>
            $<TARGET_FILE_DIR:ngram_contract_test>
    )

    add_test(NAME library-version COMMAND ngram_contract_test version)
    add_test(NAME library-options-default COMMAND ngram_contract_test options-default)
    add_test(NAME library-options-load-env COMMAND ngram_contract_test options-load-env)
    add_test(NAME library-options-free COMMAND ngram_contract_test options-free)
    add_test(NAME library-open COMMAND ngram_contract_test open)
    add_test(NAME library-close COMMAND ngram_contract_test close)
    add_test(NAME library-stop COMMAND ngram_contract_test stop)
    add_test(NAME library-execute COMMAND ngram_contract_test execute)
    add_test(NAME library-execute-span COMMAND ngram_contract_test execute-span)
    add_test(NAME library-on-signal COMMAND ngram_contract_test on-signal)
    add_test(NAME library-raise-signal COMMAND ngram_contract_test raise-signal)
    add_test(NAME library-listen-signals COMMAND ngram_contract_test listen-signals)
    add_test(NAME library-listen-signal COMMAND ngram_contract_test listen-signal)
    add_test(NAME library-signal-listener COMMAND ngram_contract_test signal-listener)
    add_test(NAME library-multictx COMMAND ngram_contract_test multictx)
endif()
