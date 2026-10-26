mingol_add_test(mingol_assets_tests
  SOURCES test_image.cpp test_audio.cpp test_model.cpp
  LIBS Mingol::Assets)
target_compile_definitions(mingol_assets_tests PRIVATE MINGOL_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
