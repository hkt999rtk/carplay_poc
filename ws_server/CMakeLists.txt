cmake_minimum_required(VERSION 3.12)

project(ws_server C CXX)
# OpenCV removed - not needed for core WebSocket functionality

add_library(
	ws_server
	${CMAKE_CURRENT_LIST_DIR}/src/base64.c
	${CMAKE_CURRENT_LIST_DIR}/src/handshake.c
	${CMAKE_CURRENT_LIST_DIR}/src/sha1.c
	${CMAKE_CURRENT_LIST_DIR}/src/utf8.c
	${CMAKE_CURRENT_LIST_DIR}/src/ws.c
)

# OpenCV include directories removed

target_include_directories(
	ws_server
	PUBLIC
	${CMAKE_CURRENT_LIST_DIR}/include
)

add_executable(
	wsh264
	${CMAKE_CURRENT_LIST_DIR}/wsh264/wsh264.cpp
	${CMAKE_CURRENT_LIST_DIR}/wsh264/osd.cpp
	${CMAKE_CURRENT_LIST_DIR}/wsh264/bitrate_ctrl.c
	${CMAKE_CURRENT_LIST_DIR}/cJSON/cJSON.c
	${CMAKE_CURRENT_LIST_DIR}/wsh264/char_queue.cpp
)

# ffmpeg_encode removed - requires OpenCV for webcam capture
# add_executable(
# 	ffmpeg_encode
# 	${CMAKE_CURRENT_LIST_DIR}/wsh264/hexdump.cpp
# 	${CMAKE_CURRENT_LIST_DIR}/wsh264/char_queue.cpp
# 	${CMAKE_CURRENT_LIST_DIR}/wsh264/ffmpeg_encode.cpp
# )

target_include_directories(
	wsh264
	PUBLIC
	${CMAKE_CURRENT_LIST_DIR}/include
	${CMAKE_CURRENT_LIST_DIR}/cJSON
)

target_link_libraries(
	wsh264
	PUBLIC
	ws_server
)

enable_testing()

add_subdirectory(from_scratch)
