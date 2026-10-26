add_executable(mingol-editor main.cpp)
target_link_libraries(mingol-editor PRIVATE Mingol::Editor Mingol::GuiGpu Mingol::Platform Mingol::RHI Mingol::Renderer)
mingol_target_options(mingol-editor)
