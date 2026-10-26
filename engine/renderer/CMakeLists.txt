add_library(mingol_renderer STATIC
  src/animation.cpp
  src/mesh.cpp
  src/shaders.cpp
  src/renderer.cpp
)
add_library(Mingol::Renderer ALIAS mingol_renderer)
target_include_directories(mingol_renderer PUBLIC include PRIVATE src)
target_link_libraries(mingol_renderer PUBLIC Mingol::Core Mingol::RHI PRIVATE Mingol::Shader)
mingol_target_options(mingol_renderer)
