add_library(mingol_scene STATIC
  src/transform.cpp
  src/scene.cpp
  src/components.cpp
)
add_library(Mingol::Scene ALIAS mingol_scene)
target_include_directories(mingol_scene PUBLIC include PRIVATE src)
target_link_libraries(mingol_scene PUBLIC Mingol::Core Mingol::Renderer)
mingol_target_options(mingol_scene)
