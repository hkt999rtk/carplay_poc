cmake_minimum_required(VERSION 3.12)

#---------------------------------------------------------------------------------------
# Set toolchain paths
#---------------------------------------------------------------------------------------
#set(TOOLCHAIN arm-none-eabi)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED YES)

if (NOT DEFINED TOOLCHAIN_PREFIX)
    if(CMAKE_HOST_SYSTEM_NAME STREQUAL Linux)
		set(TOOLCHAIN_PREFIX "/home/kevin/tools/asdk-10.3.0/linux/newlib")
    elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL Darwin)
        set(TOOLCHAIN_PREFIX "/Applications/ARM")
    elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL Windows)
        message(STATUS "Please specify the TOOLCHAIN_PREFIX !\n For example: -DTOOLCHAIN_PREFIX=\"C:/Program Files/GNU Tools ARM Embedded\" ")
    else()
        set(TOOLCHAIN_PREFIX "/usr")
        message(STATUS "No TOOLCHAIN_PREFIX specified, using default: " ${TOOLCHAIN_PREFIX})
    endif()
endif()

set(TOOLCHAIN_BIN_DIR ${TOOLCHAIN_PREFIX}/bin)
set(TOOLCHAIN_INC_DIR ${TOOLCHAIN_PREFIX}/${TOOLCHAIN}/include)
set(TOOLCHAIN_LIB_DIR ${TOOLCHAIN_PREFIX}/${TOOLCHAIN}/lib)

# Perform compiler test with static library
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

#---------------------------------------------------------------------------------------
# Set compiler/linker flags
#---------------------------------------------------------------------------------------
if(NOT DEFINED TOOLCHAIN)
	set(OBJECT_GEN_FLAGS "-O2 -fno-builtin -Wall -ffunction-sections -fdata-sections -fomit-frame-pointer")
else()
	set(OBJECT_GEN_FLAGS "-O2 -mthumb -fno-builtin -Wall -ffunction-sections -fdata-sections -fomit-frame-pointer -mabi=aapcs")
endif()

set(CMAKE_C_FLAGS   "${OBJECT_GEN_FLAGS} -std=gnu99 -Wno-unknown-pragmas" CACHE INTERNAL "C Compiler options")
set(CMAKE_CXX_FLAGS "${OBJECT_GEN_FLAGS} -std=c++14 -Wno-unknown-pragmas" CACHE INTERNAL "C++ Compiler options")
set(CMAKE_ASM_FLAGS "${OBJECT_GEN_FLAGS} -x assembler-with-cpp -Wno-unknown-pragmas" CACHE INTERNAL "ASM Compiler options")


# -Wl,--gc-sections     Perform the dead code elimination.
# --specs=nano.specs    Link with newlib-nano.
# --specs=nosys.specs   No syscalls, provide empty implementations for the POSIX system calls.
if (DEFINED TOOLCHAIN)
	#	set(CMAKE_EXE_LINKER_FLAGS "-Wl,--gc-sections --specs=nano.specs --specs=nosys.specs -mthumb -mabi=aapcs -Wl,-Map=${CMAKE_PROJECT_NAME}.map" CACHE INTERNAL "Linker options")
	set(CMAKE_EXE_LINKER_FLAGS "-Wl,--gc-sections -mthumb -mabi=aapcs -Wl,-Map=${CMAKE_PROJECT_NAME}.map" CACHE INTERNAL "Linker options")
endif()

#---------------------------------------------------------------------------------------
# Set debug/release build configuration Options
#---------------------------------------------------------------------------------------

# Options for DEBUG build
# -Og   Enables optimizations that do not interfere with debugging.
# -g    Produce debugging information in the operating system’s native format.
set(CMAKE_C_FLAGS_DEBUG "-Og -g" CACHE INTERNAL "C Compiler options for debug build type")
set(CMAKE_CXX_FLAGS_DEBUG "-Og -g" CACHE INTERNAL "C++ Compiler options for debug build type")
set(CMAKE_ASM_FLAGS_DEBUG "-g" CACHE INTERNAL "ASM Compiler options for debug build type")
set(CMAKE_EXE_LINKER_FLAGS_DEBUG "" CACHE INTERNAL "Linker options for debug build type")

# Options for RELEASE build
# -Os   Optimize for size. -Os enables all -O2 optimizations.
# -flto Runs the standard link-time optimizer.
set(CMAKE_C_FLAGS_RELEASE "-Os -flto" CACHE INTERNAL "C Compiler options for release build type")
set(CMAKE_CXX_FLAGS_RELEASE "-Os -flto" CACHE INTERNAL "C++ Compiler options for release build type")
set(CMAKE_ASM_FLAGS_RELEASE "" CACHE INTERNAL "ASM Compiler options for release build type")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-flto" CACHE INTERNAL "Linker options for release build type")


#---------------------------------------------------------------------------------------
# Set compilers
#---------------------------------------------------------------------------------------
if(NOT DEFINED TOOLCHAIN)
	set(CMAKE_C_COMPILER gcc CACHE INTERNAL "C Compiler")
	set(CMAKE_CXX_COMPILER g++ CACHE INTERNAL "C++ Compiler")
	set(CMAKE_ASM_COMPILER gcc CACHE INTERNAL "ASM Compiler")
else()
	set(CMAKE_C_COMPILER ${TOOLCHAIN_BIN_DIR}/${TOOLCHAIN}-gcc${TOOLCHAIN_EXT} CACHE INTERNAL "C Compiler")
	set(CMAKE_CXX_COMPILER ${TOOLCHAIN_BIN_DIR}/${TOOLCHAIN}-g++${TOOLCHAIN_EXT} CACHE INTERNAL "C++ Compiler")
	set(CMAKE_ASM_COMPILER ${TOOLCHAIN_BIN_DIR}/${TOOLCHAIN}-gcc${TOOLCHAIN_EXT} CACHE INTERNAL "ASM Compiler")
endif()

project(tinyhttpd C CXX ASM)

add_library(
	tinyhttpd
	${CMAKE_CURRENT_LIST_DIR}/minihttpd.cpp
)

target_include_directories(
	tinyhttpd
	PUBLIC
	${CMAKE_CURRENT_LIST_DIR}/include
)

add_executable(
	genbin
	${CMAKE_CURRENT_LIST_DIR}/genbin/genbin.cpp
)

add_executable(
	amebatest
	${CMAKE_CURRENT_LIST_DIR}/ameba/httpd.cpp
	${CMAKE_CURRENT_LIST_DIR}/ameba/freertos.c
	${CMAKE_CURRENT_LIST_DIR}/ameba/example_httpd.c
	${CMAKE_CURRENT_LIST_DIR}/ministd/avl_bf.c
)

target_include_directories(
	amebatest
	PUBLIC
	${CMAKE_CURRENT_LIST_DIR}/ameba/sys_include
	${CMAKE_CURRENT_LIST_DIR}
)


add_custom_command(
	TARGET ${CMAKE_PROJECT_NAME} PRE_BUILD
	COMMAND ${CMAKE_COMMAND} -E create_symlink
	${CMAKE_SOURCE_DIR}/htdocs $<TARGET_FILE_DIR:${PROJECT_NAME}>/htdocs
)

add_custom_command(
	TARGET genbin POST_BUILD
	COMMAND genbin
	COMMAND xxd -i htdocs.bin > htdocs_bin.c
)

add_custom_command(
	TARGET genbin POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy
	${CMAKE_CURRENT_BINARY_DIR}/*.c
	${CMAKE_CURRENT_SOURCE_DIR}/ameba
)

target_link_libraries(
	amebatest
	PUBLIC
	tinyhttpd
)
