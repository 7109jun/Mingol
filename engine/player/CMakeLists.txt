add_library(mingol_player_lib STATIC
  src/content_source.cpp
  src/game_app.cpp
)
add_library(Mingol::Player ALIAS mingol_player_lib)
target_include_directories(mingol_player_lib PUBLIC include PRIVATE src)
target_link_libraries(mingol_player_lib PUBLIC Mingol::Core Mingol::Package Mingol::Runtime Mingol::SceneIO Mingol::Scripting Mingol::Assets Mingol::Renderer Mingol::RHI)
mingol_target_options(mingol_player_lib)
