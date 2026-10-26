add_library(mingol_gui STATIC
  src/font.cpp
  src/draw.cpp
  src/raster.cpp
  src/text_edit.cpp
  src/context.cpp
  src/widgets.cpp
  src/dock.cpp
)
add_library(Mingol::Gui ALIAS mingol_gui)
target_include_directories(mingol_gui PUBLIC include PRIVATE src)
target_link_libraries(mingol_gui PUBLIC Mingol::Core Mingol::Platform)
mingol_target_options(mingol_gui)

# GPU backend (kept apart so the GUI core has no RHI/shader-compiler dependency)
add_library(mingol_gui_gpu STATIC src/gui_renderer.cpp)
add_library(Mingol::GuiGpu ALIAS mingol_gui_gpu)
target_include_directories(mingol_gui_gpu PUBLIC include PRIVATE src)
target_link_libraries(mingol_gui_gpu PUBLIC Mingol::Gui Mingol::RHI PRIVATE Mingol::Shader)
mingol_target_options(mingol_gui_gpu)
