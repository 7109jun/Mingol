mingol_add_test(mingol_editor_tests
  SOURCES test_support.cpp test_document.cpp test_viewport_math.cpp test_app.cpp test_code_editor.cpp
  LIBS Mingol::Editor)

mingol_add_test(mingol_editor_gpu_tests
  SOURCES test_document_gpu.cpp test_app_gpu.cpp
  LIBS Mingol::Editor Mingol::GuiGpu Mingol::RHI)
target_include_directories(mingol_editor_gpu_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../renderer)
