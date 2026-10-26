# Third-party single-header decoders live in their own target so the project's -Werror flags do
# not apply to code we do not own.
add_library(mingol_thirdparty_decoders STATIC src/third_party_impl.cpp)
target_include_directories(mingol_thirdparty_decoders SYSTEM PUBLIC
  ${PROJECT_SOURCE_DIR}/third_party/stb
  ${PROJECT_SOURCE_DIR}/third_party/cgltf)
if(MSVC)
  target_compile_options(mingol_thirdparty_decoders PRIVATE /W0)
else()
  target_compile_options(mingol_thirdparty_decoders PRIVATE -w)
endif()
set_target_properties(mingol_thirdparty_decoders PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(mingol_assets STATIC
  src/image.cpp
  src/audio.cpp
  src/model.cpp
)
add_library(Mingol::Assets ALIAS mingol_assets)
target_include_directories(mingol_assets PUBLIC include PRIVATE src)
target_link_libraries(mingol_assets PUBLIC Mingol::Core Mingol::Renderer PRIVATE mingol_thirdparty_decoders)
mingol_target_options(mingol_assets)
