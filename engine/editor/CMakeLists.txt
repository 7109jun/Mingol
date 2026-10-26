add_library(mingol_editor STATIC
  src/console.cpp
  src/profiler.cpp
  src/euler.cpp
  src/properties.cpp
  src/document.cpp
  src/code_editor.cpp
  src/viewport_math.cpp
  src/editor_app.cpp
  src/editor_panels.cpp
  src/editor_viewport.cpp
)
add_library(Mingol::Editor ALIAS mingol_editor)
target_include_directories(mingol_editor PUBLIC include PRIVATE src)
target_link_libraries(mingol_editor PUBLIC Mingol::Gui Mingol::Core Mingol::Runtime Mingol::SceneIO Mingol::Scripting
  Mingol::Package Mingol::Assets Mingol::Renderer Mingol::RHI Mingol::Script)
mingol_target_options(mingol_editor)
