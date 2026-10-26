add_library(mingol_sceneio STATIC
  src/asset_table.cpp
  src/scene_io.cpp
  src/builtin_components.cpp
  src/standard_assets.cpp
)
add_library(Mingol::SceneIO ALIAS mingol_sceneio)
target_include_directories(mingol_sceneio PUBLIC include PRIVATE src)
target_link_libraries(mingol_sceneio PUBLIC Mingol::Core Mingol::Scene Mingol::Runtime Mingol::Scripting)
mingol_target_options(mingol_sceneio)
