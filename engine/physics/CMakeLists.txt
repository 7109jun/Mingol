find_package(Bullet REQUIRED)

add_library(mingol_physics STATIC src/physics.cpp)
add_library(Mingol::Physics ALIAS mingol_physics)
target_include_directories(mingol_physics PUBLIC include PRIVATE src)
# Bullet's headers are not warning-clean under -Wpedantic/-Wshadow: treat them as system headers.
target_include_directories(mingol_physics SYSTEM PRIVATE ${BULLET_INCLUDE_DIRS})
target_link_libraries(mingol_physics PUBLIC Mingol::Core PRIVATE ${BULLET_LIBRARIES})
mingol_target_options(mingol_physics)
