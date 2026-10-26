add_executable(mingol-pack main.cpp)
target_link_libraries(mingol-pack PRIVATE Mingol::Package)
mingol_target_options(mingol-pack)
