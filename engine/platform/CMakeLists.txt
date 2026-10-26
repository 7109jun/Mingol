# GLFW is optional at configure time: without it only the headless backend is built and
# Window::create(WindowBackend::Glfw) reports Unsupported. Linux: libglfw3-dev. Windows: vcpkg
# `glfw3` (find_package config) or any GLFW 3.3+ install visible to CMake.
option(MINGOL_WITH_GLFW "Build the GLFW window backend (auto-disabled when GLFW is not found)" ON)

set(MINGOL_PLATFORM_SOURCES src/input.cpp src/event.cpp src/window.cpp src/headless_window.cpp)

set(_mingol_glfw_target "")
if(MINGOL_WITH_GLFW)
  find_package(glfw3 3.3 QUIET CONFIG)
  if(TARGET glfw)
    set(_mingol_glfw_target glfw)
  else()
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
      pkg_check_modules(MINGOL_GLFW_PC QUIET IMPORTED_TARGET glfw3>=3.3)
      if(TARGET PkgConfig::MINGOL_GLFW_PC)
        set(_mingol_glfw_target PkgConfig::MINGOL_GLFW_PC)
      endif()
    endif()
  endif()
  if(NOT _mingol_glfw_target)
    message(STATUS "GLFW not found: building the platform module headless-only")
  endif()
endif()

if(_mingol_glfw_target)
  list(APPEND MINGOL_PLATFORM_SOURCES src/glfw_window.cpp src/glfw_native.cpp)
endif()

add_library(mingol_platform STATIC ${MINGOL_PLATFORM_SOURCES})
add_library(Mingol::Platform ALIAS mingol_platform)
target_include_directories(mingol_platform PUBLIC include PRIVATE src)
target_link_libraries(mingol_platform PUBLIC Mingol::Core)
mingol_target_options(mingol_platform)

if(_mingol_glfw_target)
  target_compile_definitions(mingol_platform PUBLIC MINGOL_HAVE_GLFW=1)
  target_link_libraries(mingol_platform PRIVATE ${_mingol_glfw_target})
endif()
