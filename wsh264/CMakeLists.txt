cmake_minimum_required(VERSION 3.12)

if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
	project(wsh264 C CXX)
endif()

find_package(Threads REQUIRED)

add_library(
	wsh264_chacha
	${CMAKE_CURRENT_LIST_DIR}/../3rd_party/chacha/chacha.c
)

target_include_directories(
	wsh264_chacha
	PUBLIC
	${CMAKE_CURRENT_LIST_DIR}/../3rd_party/chacha
)

add_library(
	wsh264_ws
	${CMAKE_CURRENT_LIST_DIR}/src/base64.c
	${CMAKE_CURRENT_LIST_DIR}/src/handshake.c
	${CMAKE_CURRENT_LIST_DIR}/src/sha1.c
	${CMAKE_CURRENT_LIST_DIR}/src/utf8.c
	${CMAKE_CURRENT_LIST_DIR}/src/ws.c
)

target_include_directories(
	wsh264_ws
	PUBLIC
	${CMAKE_CURRENT_LIST_DIR}/include
)

add_library(
	wsh264_crypto
	${CMAKE_CURRENT_LIST_DIR}/src/crypto_stream.c
)

target_include_directories(
	wsh264_crypto
	PUBLIC
	${CMAKE_CURRENT_LIST_DIR}/include
)

target_link_libraries(
	wsh264_crypto
	PUBLIC
	wsh264_chacha
)

add_executable(
	wsh264
	${CMAKE_CURRENT_LIST_DIR}/app/wsh264.cpp
	${CMAKE_CURRENT_LIST_DIR}/app/osd.cpp
	${CMAKE_CURRENT_LIST_DIR}/app/bitrate_ctrl.c
	${CMAKE_CURRENT_LIST_DIR}/app/char_queue.cpp
	${CMAKE_CURRENT_LIST_DIR}/cJSON/cJSON.c
)

target_include_directories(
	wsh264
	PUBLIC
	${CMAKE_CURRENT_LIST_DIR}/app
	${CMAKE_CURRENT_LIST_DIR}/include
	${CMAKE_CURRENT_LIST_DIR}/cJSON
)

target_link_libraries(
	wsh264
	PUBLIC
	wsh264_ws
	wsh264_crypto
	Threads::Threads
)
