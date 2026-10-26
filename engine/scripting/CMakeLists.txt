add_library(mingol_scripting STATIC
  src/engine_api.cpp
  src/script_host.cpp
  src/script_component.cpp
)
add_library(Mingol::Scripting ALIAS mingol_scripting)
target_include_directories(mingol_scripting PUBLIC include PRIVATE src)
target_link_libraries(mingol_scripting PUBLIC Mingol::Core Mingol::Script Mingol::Scene)
mingol_target_options(mingol_scripting)
