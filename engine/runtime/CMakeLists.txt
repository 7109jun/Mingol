add_library(mingol_runtime STATIC
  src/physics_components.cpp
  src/audio_components.cpp
  src/model_instance.cpp
  src/runtime.cpp
)
add_library(Mingol::Runtime ALIAS mingol_runtime)
target_include_directories(mingol_runtime PUBLIC include PRIVATE src)
target_link_libraries(mingol_runtime PUBLIC Mingol::Core Mingol::Scene Mingol::Physics Mingol::Audio Mingol::Assets Mingol::Renderer)
mingol_target_options(mingol_runtime)
