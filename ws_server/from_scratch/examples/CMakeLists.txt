add_executable(wsfs_echo
    ${CMAKE_CURRENT_SOURCE_DIR}/wsfs_echo_server.c
)

target_include_directories(wsfs_echo
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

target_link_libraries(wsfs_echo
    PRIVATE
        wsfs
)

target_compile_features(wsfs_echo PRIVATE c_std_11)

add_executable(wsfs_selftest
    ${CMAKE_CURRENT_SOURCE_DIR}/wsfs_selftest.c
)

target_include_directories(wsfs_selftest
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

target_link_libraries(wsfs_selftest
    PRIVATE
        wsfs
)

target_compile_features(wsfs_selftest PRIVATE c_std_11)
