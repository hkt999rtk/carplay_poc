cmake_minimum_required(VERSION 3.12)
project(wsfs_from_scratch C)

option(WSFS_USE_LWIP "Build wsfs with lwIP socket layer" OFF)

set(WSFS_SOURCES
    src/wsfs_server.c
    src/wsfs_connection.c
    src/wsfs_frame_reader.c
    src/wsfs_mem.c
    ../src/base64.c
    ../src/sha1.c
    ../src/utf8.c
)

add_library(wsfs ${WSFS_SOURCES})

target_include_directories(wsfs
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
        ${CMAKE_CURRENT_SOURCE_DIR}/lwip_include
)

if (WSFS_USE_LWIP)
    target_compile_definitions(wsfs PUBLIC WSFS_PLATFORM_LWIP)
else()
    target_compile_definitions(wsfs PUBLIC WSFS_PLATFORM_BSD)
endif()

target_compile_features(wsfs PUBLIC c_std_11)

enable_testing()

add_subdirectory(examples)
add_subdirectory(tests)
