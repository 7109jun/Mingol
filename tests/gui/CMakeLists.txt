mingol_add_test(mingol_gui_tests
  SOURCES test_draw.cpp test_text_edit.cpp test_context.cpp test_dock.cpp gui_util.cpp
  LIBS Mingol::Gui)

mingol_add_test(mingol_gui_gpu_tests
  SOURCES test_gui_gpu.cpp gui_util.cpp
  LIBS Mingol::GuiGpu)
