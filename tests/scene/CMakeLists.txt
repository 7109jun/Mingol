mingol_add_test(mingol_scene_tests
  SOURCES test_scene.cpp
  LIBS Mingol::Scene)

mingol_add_test(mingol_scene_gpu_tests
  SOURCES test_scene_render.cpp
  LIBS Mingol::Scene Mingol::RHI)
target_include_directories(mingol_scene_gpu_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../renderer)
