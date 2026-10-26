find_package(Threads REQUIRED)

add_library(mingol_core STATIC
  src/assert.cpp
  src/error.cpp
  src/log.cpp
  src/math_decompose.cpp
  src/fs.cpp
  src/hash.cpp
  src/json.cpp
  src/job_system.cpp
  src/uuid.cpp
)
add_library(Mingol::Core ALIAS mingol_core)

target_include_directories(mingol_core PUBLIC include)
target_link_libraries(mingol_core PUBLIC Threads::Threads)
mingol_target_options(mingol_core)
