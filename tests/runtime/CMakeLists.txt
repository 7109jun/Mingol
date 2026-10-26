mingol_add_test(mingol_runtime_tests
  SOURCES test_runtime_physics.cpp test_runtime_audio.cpp test_runtime_loop.cpp
  LIBS Mingol::Runtime)
target_include_directories(mingol_runtime_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../audio)

mingol_add_test(mingol_runtime_gpu_tests
  SOURCES test_runtime_model.cpp
  LIBS Mingol::Runtime Mingol::RHI)
target_include_directories(mingol_runtime_gpu_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../renderer)
