add_library(mingol_package STATIC
  src/mindev.cpp
  src/crypto.cpp
  src/archive.cpp
)
add_library(Mingol::Package ALIAS mingol_package)

target_include_directories(mingol_package PUBLIC include)
target_link_libraries(mingol_package PUBLIC Mingol::Core)
mingol_target_options(mingol_package)
