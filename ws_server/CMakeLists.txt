cmake_minimum_required(VERSION 3.12)

project(ws_server C CXX)

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
find_program(NODE_EXECUTABLE node)
pkg_check_modules(GATEWAY_CLIENT_DEPS REQUIRED
	sdl2
	libavcodec
	libavutil
	libswscale
	libusb-1.0
)

link_directories(${GATEWAY_CLIENT_DEPS_LIBRARY_DIRS})

add_library(
	chacha
	${CMAKE_CURRENT_LIST_DIR}/third_party/chacha/chacha.c
)

target_include_directories(
	chacha
	PUBLIC
	${CMAKE_CURRENT_LIST_DIR}/third_party/chacha
)

add_library(
	ws_server
	${CMAKE_CURRENT_LIST_DIR}/src/base64.c
	${CMAKE_CURRENT_LIST_DIR}/src/handshake.c
	${CMAKE_CURRENT_LIST_DIR}/src/sha1.c
	${CMAKE_CURRENT_LIST_DIR}/src/utf8.c
	${CMAKE_CURRENT_LIST_DIR}/src/ws.c
)

add_library(
	ws_stream_common
	${CMAKE_CURRENT_LIST_DIR}/src/crypto_stream.c
	${CMAKE_CURRENT_LIST_DIR}/src/gateway_proto.c
)

target_include_directories(
	ws_server
	PUBLIC
	${CMAKE_CURRENT_LIST_DIR}/include
)

target_include_directories(
	ws_stream_common
	PUBLIC
	${CMAKE_CURRENT_LIST_DIR}/include
)

target_link_libraries(
	ws_stream_common
	PUBLIC
	chacha
)

add_executable(
	wsh264
	${CMAKE_CURRENT_LIST_DIR}/wsh264/wsh264.cpp
	${CMAKE_CURRENT_LIST_DIR}/wsh264/osd.cpp
	${CMAKE_CURRENT_LIST_DIR}/wsh264/bitrate_ctrl.c
	${CMAKE_CURRENT_LIST_DIR}/cJSON/cJSON.c
	${CMAKE_CURRENT_LIST_DIR}/wsh264/char_queue.cpp
)

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
	ws_stream_common
	Threads::Threads
)

add_executable(
	gateway
	${CMAKE_CURRENT_LIST_DIR}/gateway/gateway.c
	${CMAKE_CURRENT_LIST_DIR}/gateway/tcp_transport.c
	${CMAKE_CURRENT_LIST_DIR}/gateway/ws_upstream_client.c
	${CMAKE_CURRENT_LIST_DIR}/cJSON/cJSON.c
)

target_include_directories(
	gateway
	PUBLIC
	${CMAKE_CURRENT_LIST_DIR}/include
	${CMAKE_CURRENT_LIST_DIR}/gateway
	${CMAKE_CURRENT_LIST_DIR}/cJSON
)

target_link_libraries(
	gateway
	PUBLIC
	ws_stream_common
	ws_server
	Threads::Threads
)

add_executable(
	gateway_client
	${CMAKE_CURRENT_LIST_DIR}/gateway_client/gateway_client.cpp
	${CMAKE_CURRENT_LIST_DIR}/gateway_client/transport.c
	${CMAKE_CURRENT_LIST_DIR}/gateway_client/transport_libusb.c
	${CMAKE_CURRENT_LIST_DIR}/gateway/tcp_transport.c
)

target_include_directories(
	gateway_client
	PUBLIC
	${CMAKE_CURRENT_LIST_DIR}/include
	${CMAKE_CURRENT_LIST_DIR}/gateway
	${GATEWAY_CLIENT_DEPS_INCLUDE_DIRS}
)

target_link_directories(
	gateway_client
	PUBLIC
	${GATEWAY_CLIENT_DEPS_LIBRARY_DIRS}
)

target_compile_options(
	gateway_client
	PUBLIC
	${GATEWAY_CLIENT_DEPS_CFLAGS_OTHER}
)

target_link_libraries(
	gateway_client
	PUBLIC
	ws_stream_common
	Threads::Threads
	${GATEWAY_CLIENT_DEPS_LIBRARIES}
)

add_executable(
	crypto_proto_tests
	${CMAKE_CURRENT_LIST_DIR}/tests/crypto_proto_tests.c
)

target_include_directories(
	crypto_proto_tests
	PUBLIC
	${CMAKE_CURRENT_LIST_DIR}/include
)

target_link_libraries(
	crypto_proto_tests
	PUBLIC
	ws_stream_common
)

enable_testing()
add_test(NAME crypto_proto_tests COMMAND crypto_proto_tests)
if (NODE_EXECUTABLE)
	add_test(
		NAME crypto_stream_js_test
		COMMAND ${NODE_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/tests/crypto_stream_js_test.js
	)
endif()

add_subdirectory(from_scratch)
