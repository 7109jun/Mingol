find_package(glslang CONFIG REQUIRED)

add_library(mingol_shader STATIC src/shader_compiler.cpp)
add_library(Mingol::Shader ALIAS mingol_shader)

target_include_directories(mingol_shader PUBLIC include)
target_link_libraries(mingol_shader
  PUBLIC Mingol::Core
  PRIVATE glslang::glslang glslang::SPIRV glslang::glslang-default-resource-limits)
mingol_target_options(mingol_shader)

# The distro glslang is built without RTTI; UBSan's vptr check would need its typeinfo symbols.
if(MINGOL_SANITIZE AND NOT MSVC)
  target_compile_options(mingol_shader PRIVATE -fno-sanitize=vptr)
endif()
