cmake_minimum_required(VERSION 3.12)

if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
	project(gateway C CXX)
	add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../wsh264 ${CMAKE_CURRENT_BINARY_DIR}/wsh264)
endif()

find_package(Threads REQUIRED)

add_executable(
	gateway
	${CMAKE_CURRENT_LIST_DIR}/gateway.c
	${CMAKE_CURRENT_LIST_DIR}/tcp_transport.c
	${CMAKE_CURRENT_LIST_DIR}/ws_upstream_client.c
	${CMAKE_CURRENT_LIST_DIR}/../wsh264/src/gateway_proto.c
	${CMAKE_CURRENT_LIST_DIR}/../wsh264/cJSON/cJSON.c
)

target_include_directories(
	gateway
	PUBLIC
	${CMAKE_CURRENT_LIST_DIR}
	${CMAKE_CURRENT_LIST_DIR}/../wsh264/include
	${CMAKE_CURRENT_LIST_DIR}/../wsh264/cJSON
)

target_link_libraries(
	gateway
	PUBLIC
	wsh264_ws
	wsh264_crypto
	Threads::Threads
)

set_target_properties(
	gateway
	PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
