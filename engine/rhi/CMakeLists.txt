find_package(Vulkan REQUIRED)

add_library(mingol_rhi STATIC
  src/device.cpp
  src/vulkan/vk_device.cpp
  src/vulkan/vk_command_list.cpp
  src/vulkan/vk_swapchain.cpp
  src/vulkan/vma_impl.cpp
)
add_library(Mingol::RHI ALIAS mingol_rhi)

target_include_directories(mingol_rhi PUBLIC include)
target_include_directories(mingol_rhi PRIVATE src/vulkan)
# Vendored header; SYSTEM so third-party warnings never break our -Werror build.
target_include_directories(mingol_rhi SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/third_party/vma)
target_link_libraries(mingol_rhi PUBLIC Mingol::Core PRIVATE Vulkan::Vulkan)
mingol_target_options(mingol_rhi)
# `VkFooInfo info{VK_STRUCTURE_TYPE_FOO};` (remaining fields zero) is the intended Vulkan idiom.
if(NOT MSVC)
  target_compile_options(mingol_rhi PRIVATE -Wno-missing-field-initializers)
endif()

# Window-system surface support compiled into the backend.
if(WIN32)
  target_compile_definitions(mingol_rhi PRIVATE VK_USE_PLATFORM_WIN32_KHR NOMINMAX WIN32_LEAN_AND_MEAN)
else()
  find_package(X11)
  if(X11_FOUND)
    target_compile_definitions(mingol_rhi PRIVATE VK_USE_PLATFORM_XLIB_KHR)
    target_include_directories(mingol_rhi SYSTEM PRIVATE ${X11_INCLUDE_DIR})
  endif()
  find_path(XCB_INCLUDE_DIR xcb/xcb.h)
  if(XCB_INCLUDE_DIR)
    target_compile_definitions(mingol_rhi PRIVATE VK_USE_PLATFORM_XCB_KHR)
    target_include_directories(mingol_rhi SYSTEM PRIVATE ${XCB_INCLUDE_DIR})
  endif()
  find_path(WAYLAND_INCLUDE_DIR wayland-client.h)
  if(WAYLAND_INCLUDE_DIR)
    target_compile_definitions(mingol_rhi PRIVATE VK_USE_PLATFORM_WAYLAND_KHR)
    target_include_directories(mingol_rhi SYSTEM PRIVATE ${WAYLAND_INCLUDE_DIR})
  endif()
endif()

# The vendored allocator is compiled without our warning policy.
if(MSVC)
  set_source_files_properties(src/vulkan/vma_impl.cpp PROPERTIES COMPILE_OPTIONS "/w")
else()
  set_source_files_properties(src/vulkan/vma_impl.cpp PROPERTIES COMPILE_OPTIONS "-w")
endif()
