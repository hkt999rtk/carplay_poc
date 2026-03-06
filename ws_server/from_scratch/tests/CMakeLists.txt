find_package(Threads REQUIRED)

add_executable(wsfs_frame_reader_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/wsfs_frame_reader_tests.c
)

target_include_directories(wsfs_frame_reader_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
        ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

target_link_libraries(wsfs_frame_reader_tests
    PRIVATE
        wsfs
)

target_compile_features(wsfs_frame_reader_tests PRIVATE c_std_11)

add_test(NAME wsfs_frame_reader COMMAND wsfs_frame_reader_tests)

add_executable(wsfs_send_queue_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/wsfs_send_queue_tests.c
)

target_include_directories(wsfs_send_queue_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
        ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

target_link_libraries(wsfs_send_queue_tests
    PRIVATE
        wsfs
)

target_compile_features(wsfs_send_queue_tests PRIVATE c_std_11)

add_test(NAME wsfs_send_queue COMMAND wsfs_send_queue_tests)

add_executable(wsfs_echo_integration
    ${CMAKE_CURRENT_SOURCE_DIR}/wsfs_echo_integration.c
)

target_include_directories(wsfs_echo_integration
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
        ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

target_link_libraries(wsfs_echo_integration
    PRIVATE
        wsfs
        Threads::Threads
)

target_compile_features(wsfs_echo_integration PRIVATE c_std_11)

add_test(NAME wsfs_echo_integration COMMAND wsfs_echo_integration)

add_executable(wsfs_handshake_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/wsfs_handshake_tests.c
)

target_include_directories(wsfs_handshake_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
        ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

target_link_libraries(wsfs_handshake_tests
    PRIVATE
        wsfs
        Threads::Threads
)

target_compile_features(wsfs_handshake_tests PRIVATE c_std_11)

add_test(NAME wsfs_handshake COMMAND wsfs_handshake_tests)

add_test(NAME wsfs_selftest COMMAND wsfs_selftest)

add_executable(wsfs_c2c_stress
    ${CMAKE_CURRENT_SOURCE_DIR}/wsfs_c2c_stress.c
)

target_include_directories(wsfs_c2c_stress
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
        ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

target_link_libraries(wsfs_c2c_stress
    PRIVATE
        wsfs
        Threads::Threads
)

target_compile_features(wsfs_c2c_stress PRIVATE c_std_11)

add_executable(wsfs_js_server_stress
    ${CMAKE_CURRENT_SOURCE_DIR}/wsfs_js_server_stress.c
)

target_compile_features(wsfs_js_server_stress PRIVATE c_std_11)

add_executable(wsfs_c_js_client_stress
    ${CMAKE_CURRENT_SOURCE_DIR}/wsfs_c_js_client_stress.c
)

target_include_directories(wsfs_c_js_client_stress
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
        ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

target_link_libraries(wsfs_c_js_client_stress
    PRIVATE
        wsfs
        Threads::Threads
)

target_compile_features(wsfs_c_js_client_stress PRIVATE c_std_11)
