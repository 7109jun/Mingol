mingol_add_test(mingol_player_tests
  SOURCES test_game_app.cpp
  LIBS Mingol::Player Mingol::Editor)
target_include_directories(mingol_player_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../editor)

mingol_add_test(mingol_player_gpu_tests
  SOURCES test_game_app_gpu.cpp
  LIBS Mingol::Player Mingol::Editor Mingol::RHI)
target_include_directories(mingol_player_gpu_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../renderer ${CMAKE_CURRENT_SOURCE_DIR}/../editor)

# The end-to-end tests launch the real executable.
add_dependencies(mingol_player_gpu_tests mingol-player)
target_compile_definitions(mingol_player_gpu_tests PRIVATE MINGOL_PLAYER_EXE="$<TARGET_FILE:mingol-player>")
