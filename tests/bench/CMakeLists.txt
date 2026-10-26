# Micro benchmarks (not part of ctest; run build/<preset>/tests/bench/mingol-bench).
add_executable(mingol-bench bench.cpp)
target_link_libraries(mingol-bench PRIVATE Mingol::Player Mingol::Editor Mingol::RHI)
target_include_directories(mingol-bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../editor ${CMAKE_CURRENT_SOURCE_DIR}/..)
mingol_target_options(mingol-bench)
