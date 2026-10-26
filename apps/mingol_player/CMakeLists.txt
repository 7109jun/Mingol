add_executable(mingol-player main.cpp)
target_link_libraries(mingol-player PRIVATE Mingol::Player Mingol::Platform Mingol::RHI Mingol::Renderer Mingol::Package)
mingol_target_options(mingol-player)
