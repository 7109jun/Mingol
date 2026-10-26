find_package(Threads REQUIRED)

# miniaudio's implementation is third-party code: built without the project's warning flags.
add_library(mingol_miniaudio STATIC src/miniaudio_impl.cpp)
target_include_directories(mingol_miniaudio SYSTEM PUBLIC ${PROJECT_SOURCE_DIR}/third_party/miniaudio)
set_target_properties(mingol_miniaudio PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
  target_compile_options(mingol_miniaudio PRIVATE /W0)
else()
  target_compile_options(mingol_miniaudio PRIVATE -w)
endif()
target_link_libraries(mingol_miniaudio PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
if(NOT WIN32)
  target_link_libraries(mingol_miniaudio PUBLIC m)
endif()

add_library(mingol_audio STATIC
  src/mixer.cpp
  src/device.cpp
)
add_library(Mingol::Audio ALIAS mingol_audio)
target_include_directories(mingol_audio PUBLIC include PRIVATE src)
target_link_libraries(mingol_audio PUBLIC Mingol::Core Mingol::Assets PRIVATE mingol_miniaudio)
mingol_target_options(mingol_audio)
