// SPDX-License-Identifier: GPL-2.0-only
// Single translation unit that compiles Vulkan Memory Allocator (MIT, see third_party/vma).
// Vulkan entry points come from the statically linked loader.
#define VMA_IMPLEMENTATION
#define VMA_STATIC_VULKAN_FUNCTIONS 1
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 0
#include "vk_mem_alloc.h"
