cmake_minimum_required(VERSION 3.12)

if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
	project(gateway_client C CXX)
	add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../wsh264 ${CMAKE_CURRENT_BINARY_DIR}/wsh264)
endif()

find_package(Threads REQUIRED)
find_package(PkgConfig QUIET)

if (PkgConfig_FOUND)
	pkg_check_modules(GATEWAY_CLIENT_DEPS QUIET
		sdl2
		libavcodec
		libavutil
		libswscale
		libusb-1.0
	)
endif()

if (PkgConfig_FOUND AND GATEWAY_CLIENT_DEPS_FOUND)
	link_directories(${GATEWAY_CLIENT_DEPS_LIBRARY_DIRS})

	add_executable(
		gateway_client
		${CMAKE_CURRENT_LIST_DIR}/gateway_client.cpp
		${CMAKE_CURRENT_LIST_DIR}/transport.c
		${CMAKE_CURRENT_LIST_DIR}/transport_libusb.c
		${CMAKE_CURRENT_LIST_DIR}/../gateway/tcp_transport.c
		${CMAKE_CURRENT_LIST_DIR}/../wsh264/src/gateway_proto.c
	)

	target_include_directories(
		gateway_client
		PUBLIC
		${CMAKE_CURRENT_LIST_DIR}
		${CMAKE_CURRENT_LIST_DIR}/../gateway
		${CMAKE_CURRENT_LIST_DIR}/../wsh264/include
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
		wsh264_crypto
		Threads::Threads
		${GATEWAY_CLIENT_DEPS_LIBRARIES}
	)

	if (WIN32)
		target_link_libraries(
			gateway_client
			PUBLIC
			ws2_32
		)
	endif()

	set_target_properties(
		gateway_client
		PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
	)
else()
	message(STATUS "gateway_client disabled: missing pkg-config or SDL2/libav/libusb modules")
endif()
