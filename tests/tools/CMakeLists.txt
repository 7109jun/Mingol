# End-to-end test of the mingol-pack command line tool (POSIX shell semantics for env vars are handled by CMake).
add_test(NAME mingol_pack_cli
  COMMAND ${CMAKE_COMMAND}
    -DPACK=$<TARGET_FILE:mingol-pack>
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/cli_work
    -P ${CMAKE_CURRENT_SOURCE_DIR}/pack_cli_test.cmake)
