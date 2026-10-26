mingol_add_test(mingol_sceneio_tests
  SOURCES test_sceneio.cpp test_sceneio_errors.cpp
  LIBS Mingol::SceneIO)

mingol_add_test(mingol_sceneio_gpu_tests
  SOURCES test_sceneio_gpu.cpp
  LIBS Mingol::SceneIO Mingol::RHI)
target_include_directories(mingol_sceneio_gpu_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../renderer)
